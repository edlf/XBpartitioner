// File system check, repair and resizing on the console (see fatx_check and fatx_resize).

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"
#include "fatx_check.hpp"
#include "fatx_resize.hpp"

// One partition, accessed through the whole-disk device
class DiskPartitionDevice : public FatxDevice
{
public:
    DiskPartitionDevice(ULONGLONG start_lba, bool writable)
        : m_base(start_lba * HDD_SECTOR_SIZE), m_disk(NULL), m_writable(writable)
    {
        ANSI_STRING path;
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK status_block;
        RtlInitAnsiString(&path, "\\Device\\Harddisk0\\Partition0");
        InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, NULL);
        const ACCESS_MASK access = SYNCHRONIZE | GENERIC_READ | (writable ? GENERIC_WRITE : 0);
        if (NtOpenFile(&m_disk, access, &attributes, &status_block, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       FILE_SYNCHRONOUS_IO_ALERT) != 0)
        {
            m_disk = NULL;
        }
    }

    ~DiskPartitionDevice()
    {
        if (m_disk != NULL)
        {
            NtClose(m_disk);
        }
    }

    bool Read(uint64_t offset, void *buffer, uint32_t length)
    {
        if (m_disk == NULL)
        {
            return false;
        }
        LARGE_INTEGER position;
        position.QuadPart = m_base + offset;
        IO_STATUS_BLOCK status_block;
        return NtReadFile(m_disk, NULL, NULL, NULL, &status_block, buffer, length, &position) == 0;
    }

    bool Write(uint64_t offset, const void *buffer, uint32_t length)
    {
        if (m_disk == NULL || !m_writable)
        {
            return false;
        }
        LARGE_INTEGER position;
        position.QuadPart = m_base + offset;
        IO_STATUS_BLOCK status_block;
        return NtWriteFile(m_disk, NULL, NULL, NULL, &status_block, (PVOID)buffer, length, &position) == 0;
    }

private:
    ULONGLONG m_base;
    HANDLE m_disk;
    bool m_writable;
};

// Writes a resized partition's new size to the table. Runs on the resize's
// worker thread, so no dialogs: write_main is decided beforehand.
class XboxResizeTable : public FatxResizeTable
{
public:
    XboxResizeTable(App *app, int partition, const XboxPartitionTable &base, bool write_main)
        : m_app(app), m_partition(partition), m_base(base), m_write_main(write_main)
    {
    }

    bool SetSize(uint64_t new_lba_size)
    {
        bool backup_written;
        return m_app->write_tables_raw(TableWith(new_lba_size), m_write_main, &backup_written);
    }

    XboxPartitionTable TableWith(uint64_t new_lba_size)
    {
        // Sizes here are already aligned
        XboxPartitionTableManipulator table(m_base, m_app->m_TotalHddSectors, m_app->m_FullLba48, m_app->m_Physical4k);
        table.SetPartitionSize(m_partition, new_lba_size);
        return table.GetPartitionTable();
    }

private:
    App *m_app;
    int m_partition;
    XboxPartitionTable m_base;
    bool m_write_main;
};

enum FsckJobKind
{
    JOB_REPAIR,       // Check, plus a repair or (write false) a dry run
    JOB_RESIZE_CHECK, // Can a table change alone keep the data?
    JOB_MOVE_RESIZE,  // Resize with moves, or check it can be done
    JOB_RESUME        // Finish an interrupted resize
};

// Disk work for a worker thread, so the screen keeps animating
struct FsckJob : public FatxProgress
{
    FsckJobKind kind;
    ULONGLONG start_lba;
    ULONGLONG lba_size;
    bool write;
    FatxRepairResult *repair;
    ULONGLONG new_lba_size;
    FatxResizeResult *resize;
    FatxMoveResult *move;
    FatxResizeTable *table;
    volatile LONG percent; // Read by the main thread

    void Update(int value)
    {
        InterlockedExchange((LPLONG)&percent, value); // The XDK version takes a non-volatile pointer
    }
};

static DWORD WINAPI fsck_thread(LPVOID parameter)
{
    FsckJob *job = (FsckJob *)parameter;
    DiskPartitionDevice device(job->start_lba, job->write);
    switch (job->kind)
    {
    case JOB_RESIZE_CHECK:
        FatxCheckResize(device, job->lba_size, job->new_lba_size, job->resize, job);
        break;
    case JOB_MOVE_RESIZE:
        FatxResizeWithMoves(device, job->lba_size, job->new_lba_size, *job->table, job->write, job->move, job);
        break;
    case JOB_RESUME:
        FatxResumeResize(device, *job->table, job->move, job);
        break;
    default:
        FatxRepair(device, job->lba_size * HDD_SECTOR_SIZE, job->repair, job->write, job);
        break;
    }
    return 0;
}

// "Error: \games\x: ...", cut to fit a dialog line
static void format_check_message(const FatxCheckMessage &message, wchar_t *out, size_t out_count)
{
    static const size_t MAX_CHARS = 54;
    const wchar_t *prefix = message.error ? L"Error: " : L"Warning: ";
    size_t length = wcslen(prefix);
    wcscpy(out, prefix);

    const char *text = message.text;
    while (*text != '\0' && length < out_count - 1)
    {
        out[length++] = (wchar_t)(unsigned char)*text++;
    }
    out[length] = L'\0';

    if (length > MAX_CHARS)
    {
        wcscpy(out + MAX_CHARS - 3, L"...");
    }
}

// Formats a cluster count as "1.50 MB"
static void format_clusters_size(ULONGLONG clusters, uint32_t cluster_bytes, wchar_t *out)
{
    format_human_size(clusters * cluster_bytes / HDD_SECTOR_SIZE, out);
}

// No memset: the struct has a vtable
static void init_job(FsckJob *job, FsckJobKind kind)
{
    job->kind = kind;
    job->start_lba = 0;
    job->lba_size = 0;
    job->write = false;
    job->repair = NULL;
    job->new_lba_size = 0;
    job->resize = NULL;
    job->move = NULL;
    job->table = NULL;
    job->percent = 0;
}

void App::RunFsckJob(int partition, bool write, LPCWSTR verb, FatxRepairResult *repair)
{
    FsckJob job;
    init_job(&job, JOB_REPAIR);
    job.write = write;
    job.repair = repair;
    RunFsckThread(partition, verb, job);
}

void App::RunResizeCheckJob(int partition, ULONGLONG new_lba_size, FatxResizeResult *resize)
{
    FsckJob job;
    init_job(&job, JOB_RESIZE_CHECK);
    job.new_lba_size = new_lba_size;
    job.resize = resize;
    RunFsckThread(partition, L"Checking", job);
}

void App::RunMoveResizeJob(int partition, ULONGLONG new_lba_size, bool write, FatxMoveResult *move, bool write_main)
{
    XboxResizeTable table(this, partition, m_TableKernel, write_main);
    FsckJob job;
    init_job(&job, JOB_MOVE_RESIZE);
    job.write = write;
    job.new_lba_size = new_lba_size;
    job.move = move;
    job.table = &table;
    RunFsckThread(partition, write ? L"Resizing" : L"Checking", job);
}

void App::RunResumeJob(int partition, FatxMoveResult *move, bool write_main)
{
    XboxResizeTable table(this, partition, m_TableKernel, write_main);
    FsckJob job;
    init_job(&job, JOB_RESUME);
    job.write = true;
    job.move = move;
    job.table = &table;
    RunFsckThread(partition, L"Resizing", job);
}

// Uses the partition as it is on disk (m_pTableOriginal)
void App::RunFsckThread(int partition, LPCWSTR verb, FsckJob &job)
{
    wchar_t busy[60];
    swprintf(busy, L"%s partition %d (%c)...", verb, partition + 1, partition_letter(partition));

    job.start_lba = m_pTableOriginal->GetPartitionStart(partition);
    job.lba_size = m_pTableOriginal->GetPartitionSize(partition);
    job.percent = 0;
    RunWithBusyWindow(fsck_thread, &job, busy, &job.percent);
}

void App::DoCheckFileSystem()
{
    CheckFileSystem(m_CurrentPart);
}

// Checks the partition as it is on disk, not as edited
void App::CheckFileSystem(int partition)
{
    if (!m_pTableOriginal->isPartitionEnabled(partition))
    {
        ShowMessage(L"Not checked", COLOR_YELLOW, L"This partition isn't in the table on disk yet.");
        return;
    }

    // ~10 KB, too big for the stack
    FatxRepairResult *plan = new FatxRepairResult;
    if (plan == NULL)
    {
        ShowMessage(L"Not checked", COLOR_YELLOW, L"Not enough memory for the check.");
        return;
    }
    RunFsckJob(partition, false, L"Checking", plan);

    // Found something the repairs can fix
    const bool fixable = (plan->status == FATX_REPAIR_DONE);
    const bool can_repair = fixable && partition != m_LaunchPartition;
    LPCWSTR repair_line = NULL;
    if (can_repair)
    {
        repair_line = L"Repair fixes the warnings, file contents stay the same.";
    }
    else if (fixable)
    {
        repair_line = L"Not repairable here: this program runs from it.";
    }

    if (ShowCheckResult(partition, plan->check, repair_line, can_repair))
    {
        RepairFileSystem(partition, *plan);
    }
    delete plan;
}

// Returns true if the user chose Repair
bool App::ShowCheckResult(int partition, const FatxCheckResult &result, LPCWSTR extra_line, bool offer_repair)
{
    wchar_t line_summary[80];
    wchar_t line_counts[80];
    wchar_t line_lost[80];
    wchar_t line_more[40];
    wchar_t line_messages[4][FATX_CHECK_MESSAGE_LENGTH];
    LPCWSTR lines[9];
    int line_count = 0;
    LPCWSTR title = L"Not checked";
    Color title_color = COLOR_YELLOW;

    switch (result.status)
    {
    case FATX_CHECK_RESIZING:
        lines[line_count++] = L"A resize or cluster size change was interrupted.";
        lines[line_count++] = L"Finish it on a PC: fatxtool resize/recluster --resume.";
        break;
    case FATX_CHECK_NOT_FATX:
        lines[line_count++] = L"No FATX file system: the partition isn't formatted.";
        break;
    case FATX_CHECK_READ_ERROR:
        lines[line_count++] = L"The partition could not be read.";
        break;
    case FATX_CHECK_NO_MEMORY:
        lines[line_count++] = L"Not enough memory to check this partition.";
        break;
    default:
        swprintf(line_summary, L"Partition %d (%c): %I64u files, %I64u folders.", partition + 1,
                 partition_letter(partition), result.files, result.directories);
        lines[line_count++] = line_summary;

        if (result.status == FATX_CHECK_BAD_HEADER)
        {
            title = L"Errors found";
            title_color = COLOR_RED;
            swprintf(line_counts, L"The FATX header is damaged.");
            lines[0] = line_counts; // Replaces the file count, which is meaningless here
        }
        else if (result.errors == 0)
        {
            title = L"No errors";
            title_color = COLOR_GREEN;
            if (result.warnings == 0)
            {
                swprintf(line_counts, L"No errors or warnings found.");
            }
            else
            {
                swprintf(line_counts, L"No errors, %u warnings.", result.warnings);
            }
            lines[line_count++] = line_counts;
        }
        else
        {
            title = L"Errors found";
            title_color = COLOR_RED;
            swprintf(line_counts, L"%u errors, %u warnings.", result.errors, result.warnings);
            lines[line_count++] = line_counts;
        }

        if (result.lost_clusters > 0)
        {
            wchar_t size[32];
            format_clusters_size(result.lost_clusters, result.cluster_bytes, size);
            swprintf(line_lost, L"Lost space: %I64u clusters (%s).", result.lost_clusters, size);
            lines[line_count++] = line_lost;
        }
        break;
    }

    // Show a few messages, errors first
    int shown = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t i = 0; i < result.message_count && shown < (int)ARRAY_COUNT(line_messages); i++)
        {
            const bool want_error = (pass == 0);
            if (result.messages[i].error != want_error)
            {
                continue;
            }
            // Already shown as the lost space line
            if (!want_error && result.lost_clusters > 0 && strstr(result.messages[i].text, "lost space") != NULL)
            {
                continue;
            }
            format_check_message(result.messages[i], line_messages[shown], ARRAY_COUNT(line_messages[shown]));
            lines[line_count++] = line_messages[shown];
            shown++;
        }
    }
    const uint32_t total = result.errors + result.warnings - (result.lost_clusters > 0 ? 1 : 0);
    if (total > (uint32_t)shown && shown > 0)
    {
        swprintf(line_more, L"...and %u more.", total - shown);
        lines[line_count++] = line_more;
    }

    if (extra_line != NULL)
    {
        lines[line_count++] = extra_line;
    }

    if (offer_repair)
    {
        return RunDialog(title, title_color, lines, line_count, L"Repair", L"Close");
    }
    RunDialog(title, title_color, lines, line_count, L"OK", NULL);
    return false;
}

// Confirms the repairs the dry run found, unmounts, repairs and checks again
void App::RepairFileSystem(int partition, const FatxRepairResult &plan)
{
    // Never repair the partition we're running from
    if (partition == m_LaunchPartition)
    {
        ShowMessage(L"Not repaired", COLOR_YELLOW, L"This program runs from that partition.");
        return;
    }

    wchar_t line_freed[80];
    wchar_t line_trimmed[80];
    wchar_t line_cleared[80];
    LPCWSTR lines[7];
    int line_count = 0;
    if (plan.clusters_freed > 0)
    {
        wchar_t size[32];
        format_clusters_size(plan.clusters_freed, plan.check.cluster_bytes, size);
        swprintf(line_freed, L"Free %I64u unused clusters (%s).", plan.clusters_freed, size);
        lines[line_count++] = line_freed;
    }
    if (plan.files_trimmed > 0)
    {
        swprintf(line_trimmed, L"End the chains of %u files at their size.", plan.files_trimmed);
        lines[line_count++] = line_trimmed;
    }
    if (plan.empty_files_cleared > 0)
    {
        swprintf(line_cleared, L"Free the clusters of %u empty files.", plan.empty_files_cleared);
        lines[line_count++] = line_cleared;
    }
    if (plan.media_fixed)
    {
        lines[line_count++] = L"Set FAT entry 0 back to its usual value.";
    }
    lines[line_count++] = L"No file's contents change. The partition is";
    lines[line_count++] = L"unmounted first, and mounted again when next used.";
    if (!RunDialog(L"Repair", COLOR_WARNING, lines, line_count, L"Repair", L"Cancel"))
    {
        return;
    }

    // Otherwise the kernel's cached FAT could overwrite the repair
    const NTSTATUS dismounted = IoDismountVolumeByName(&m_PartitionPaths[partition]);
    if (!NT_SUCCESS(dismounted))
    {
        wchar_t error[80];
        swprintf(error, L"Unmounting failed (0x%08X): nothing was changed.", dismounted);
        ShowMessage(L"Not repaired", COLOR_RED, error);
        return;
    }

    // Re-checks before writing; refuses if errors appeared
    FatxRepairResult *repair = new FatxRepairResult;
    if (repair == NULL)
    {
        ShowMessage(L"Not repaired", COLOR_YELLOW, L"Not enough memory for the repair.");
        return;
    }
    RunFsckJob(partition, true, L"Repairing", repair);

    switch (repair->status)
    {
    case FATX_REPAIR_DONE:
    {
        FatxRepairResult *after = new FatxRepairResult;
        if (after == NULL)
        {
            ShowMessage(L"Repaired", COLOR_GREEN, L"Repaired. Check it again to see the result.");
            break;
        }
        RunFsckJob(partition, false, L"Checking", after);
        ShowCheckResult(partition, after->check, L"Repaired. This is the check afterwards.", false);
        delete after;
        break;
    }
    case FATX_REPAIR_NOTHING_TO_DO:
        ShowMessage(L"Not repaired", COLOR_YELLOW, L"There was nothing left to repair.");
        break;
    case FATX_REPAIR_HAS_ERRORS:
        ShowMessage(L"Not repaired", COLOR_RED, L"The check now finds errors: nothing was changed.");
        break;
    case FATX_REPAIR_STOPPED_PARTWAY:
        ShowMessage(L"Error", COLOR_RED, L"The repair stopped partway. Check the partition again.");
        break;
    default:
        ShowMessage(L"Not repaired", COLOR_RED, L"The partition could not be read: nothing was changed.");
        break;
    }
    delete repair;
}

int App::FindResizesKeepingData(bool kept[XboxPartitionTableEntryCount], bool moved[XboxPartitionTableEntryCount],
                                wchar_t *reason, size_t reason_count)
{
    int count = 0;
    reason[0] = L'\0';
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        kept[i] = false;
        moved[i] = false;
        if (!m_pTable->isPartitionEnabled(i) || !m_pTableOriginal->isPartitionEnabled(i) ||
            m_pTable->GetPartitionStart(i) != m_pTableOriginal->GetPartitionStart(i))
        {
            continue;
        }
        const ULONGLONG old_size = m_pTableOriginal->GetPartitionSize(i);
        const ULONGLONG new_size = m_pTable->GetPartitionSize(i);
        if (new_size == old_size || m_ClusterSizes[i] == 0)
        {
            continue; // Unchanged, or nothing to keep
        }

        // Classify by size first, so formats need no disk access
        const FatxResizeKind kind = FatxClassifyResize(old_size, m_ClusterSizes[i] * 1024, new_size);
        LPCWSTR why = NULL;
        if (kind == FATX_RESIZE_KIND_IN_PLACE)
        {
            FatxResizeResult *resize = new FatxResizeResult;
            if (resize == NULL)
            {
                continue;
            }
            RunResizeCheckJob(i, new_size, resize);
            switch (resize->status)
            {
            case FATX_RESIZE_OK:
                kept[i] = true;
                break;
            case FATX_RESIZE_CLUSTERS_IN_USE:
                why = L"the space it gives up is in use";
                break;
            case FATX_RESIZE_HAS_ERRORS:
                why = L"its file system has errors";
                break;
            case FATX_RESIZE_FAT_NOT_CLEAN:
                why = L"its FAT isn't clean past its end";
                break;
            default:
                why = L"it could not be checked";
                break;
            }
            delete resize;
        }
        else if (kind == FATX_RESIZE_KIND_MOVES)
        {
            // It gets unmounted, so not the partition we're running from
            FatxMoveResult *move = new FatxMoveResult;
            if (i == m_LaunchPartition || move == NULL)
            {
                why = L"this program runs from it";
            }
            else
            {
                RunMoveResizeJob(i, new_size, false, move, true);
                switch (move->status)
                {
                case FATX_MOVE_PLANNED:
                    moved[i] = true;
                    break;
                case FATX_MOVE_NO_ROOM:
                    why = L"not enough free space to move its clusters";
                    break;
                case FATX_MOVE_HAS_ERRORS:
                    why = L"its file system has errors";
                    break;
                default:
                    why = L"it could not be checked";
                    break;
                }
            }
            delete move;
        }

        if (kept[i] || moved[i])
        {
            count++;
        }
        else if (why != NULL && reason[0] == L'\0')
        {
            _snwprintf(reason, reason_count, L"%d (%c) can't keep its data: %s.", i + 1, partition_letter(i), why);
            reason[reason_count - 1] = L'\0';
        }
    }
    return count;
}

// Runs the shrinking or the growing resizes with moves. Each partition is
// unmounted first and writes its own size to the table. Stops at the first
// failure and explains it.
bool App::RunMoveResizes(const bool moved[XboxPartitionTableEntryCount],
                         const ULONGLONG new_sizes[XboxPartitionTableEntryCount], bool shrinking, bool write_main)
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        // The shrinking pass may already have done this one
        const ULONGLONG old_size = m_pTableOriginal->GetPartitionSize(i);
        if (!moved[i] || new_sizes[i] == old_size || (new_sizes[i] < old_size) != shrinking)
        {
            continue;
        }

        // Make sure the new size fits the current table
        XboxResizeTable check(this, i, m_TableKernel, write_main);
        XboxPartitionTable table;
        bool backup_possible;
        if (!prepare_table(check.TableWith(new_sizes[i]), &table, &backup_possible))
        {
            wchar_t line[80];
            swprintf(line, L"Partition %d (%c) wasn't resized: it doesn't fit.", i + 1, partition_letter(i));
            ShowMessage(L"Not resized", COLOR_RED, line);
            return false;
        }

        IoDismountVolumeByName(&m_PartitionPaths[i]);
        FatxMoveResult *move = new FatxMoveResult;
        if (move == NULL)
        {
            return false;
        }
        RunMoveResizeJob(i, new_sizes[i], true, move, write_main);
        const FatxMoveStatus status = move->status;
        const ULONGLONG moved_clusters = move->clusters_moved;
        delete move;

        // The worker thread changed the kernel's table; reload the editor
        reload_after_write(m_TableKernel);

        wchar_t line1[80];
        wchar_t line2[80];
        LPCWSTR lines[3];
        int line_count = 0;
        if (status == FATX_MOVE_DONE)
        {
            continue;
        }
        if (status == FATX_MOVE_STOPPED_SAFE)
        {
            swprintf(line1, L"Resizing partition %d (%c) stopped while moving", i + 1, partition_letter(i));
            lines[line_count++] = line1;
            lines[line_count++] = L"clusters: it keeps its old size and its data.";
            lines[line_count++] = L"A check may show lost space, which Repair frees.";
        }
        else if (status == FATX_MOVE_INTERRUPTED)
        {
            swprintf(line1, L"Resizing partition %d (%c) stopped partway (%I64u", i + 1, partition_letter(i), moved_clusters);
            swprintf(line2, L"clusters moved). Finish it: it's offered at the next start.");
            lines[line_count++] = line1;
            lines[line_count++] = line2;
            lines[line_count++] = L"Until then it can't be used.";
        }
        else
        {
            swprintf(line1, L"Partition %d (%c) couldn't be resized: nothing", i + 1, partition_letter(i));
            lines[line_count++] = line1;
            lines[line_count++] = L"was changed on it (check it for errors).";
        }
        RunDialog(L"Not resized", COLOR_RED, lines, line_count, L"OK", NULL);
        return false;
    }
    return true;
}

// Writes the edited table, keeping the data of resized partitions. Shrinks
// with moves go first so their space is free, growths with moves go last,
// and everything else is written in between.
bool App::WriteTableWithResizes(const bool kept[XboxPartitionTableEntryCount],
                                const bool moved[XboxPartitionTableEntryCount], bool *backup_written, bool *main_written,
                                bool *explained)
{
    *explained = false;
    const XboxPartitionTable edited = m_pTable->GetPartitionTable();
    ULONGLONG new_sizes[XboxPartitionTableEntryCount];
    bool any_moved = false;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        new_sizes[i] = m_pTable->GetPartitionSize(i);
        any_moved = any_moved || moved[i];
    }

    // Ask the Titan BIOS question once, up front
    bool write_main = true;
    if (any_moved)
    {
        XboxPartitionTable table;
        bool backup_possible;
        if (!prepare_table(edited, &table, &backup_possible) || !ChooseTablesToWrite(backup_possible, &write_main))
        {
            return false;
        }
        const LPCWSTR lines[] = {
            L"Clusters are moved and renumbered now.",
            L"Don't switch the Xbox off until it's done.",
        };
        ShowWarning(lines, ARRAY_COUNT(lines));
    }

    if (any_moved && !RunMoveResizes(moved, new_sizes, true, write_main))
    {
        *explained = true;
        return false;
    }

    // Everything else; partitions still to grow keep their old size for now
    XboxPartitionTableManipulator rest(edited, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (moved[i] && new_sizes[i] > m_pTableOriginal->GetPartitionSize(i))
        {
            rest.SetPartitionSize(i, m_pTableOriginal->GetPartitionSize(i));
        }
    }
    if (!write_partition_tables(rest.GetPartitionTable(), backup_written, main_written))
    {
        return false;
    }
    UnmountResized(kept);

    if (any_moved && !RunMoveResizes(moved, new_sizes, false, write_main))
    {
        *explained = true;
        return false;
    }
    return true;
}

// At startup: offers to finish interrupted resizes. Until then the
// partition is unusable (its signature reads "XRSZ").
void App::OfferFinishResizes()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!m_pTableOriginal->isPartitionEnabled(i) || m_pKernelTable == NULL)
        {
            continue;
        }
        DiskPartitionDevice device(m_pTableOriginal->GetPartitionStart(i), false);
        unsigned char sector[HDD_SECTOR_SIZE];
        uint32_t signature = 0;
        if (device.Read(0, sector, sizeof(sector)))
        {
            memcpy(&signature, sector, 4);
        }
        uint64_t old_size, new_size;
        const bool journal = FatxFindInterruptedResize(device, &old_size, &new_size);
        if (signature != FATX_RESIZING_SIGNATURE && !journal)
        {
            continue;
        }

        wchar_t line1[80];
        if (!journal)
        {
            swprintf(line1, L"Changing partition %d (%c)'s cluster size was", i + 1, partition_letter(i));
            const LPCWSTR lines[] = {
                line1,
                L"interrupted on a PC. Finish it there:",
                L"fatxtool recluster <disk image> --resume.",
            };
            ShowWarning(lines, ARRAY_COUNT(lines));
            continue;
        }

        wchar_t new_size_text[32];
        format_human_size(new_size, new_size_text);
        swprintf(line1, L"Resizing partition %d (%c) to %s was interrupted.", i + 1, partition_letter(i), new_size_text);
        const LPCWSTR lines[] = {
            line1,
            L"It can't be used until that's finished.",
            L"Finish it now? Don't switch off meanwhile.",
        };
        if (i == m_LaunchPartition ||
            !RunDialog(L"Interrupted resize", COLOR_WARNING, lines, ARRAY_COUNT(lines), L"Finish", L"Later"))
        {
            continue;
        }

        XboxPartitionTable table;
        bool backup_possible;
        bool write_main = true;
        if (!prepare_table(m_TableKernel, &table, &backup_possible) || !ChooseTablesToWrite(backup_possible, &write_main))
        {
            continue;
        }
        IoDismountVolumeByName(&m_PartitionPaths[i]);
        FatxMoveResult *move = new FatxMoveResult;
        if (move == NULL)
        {
            continue;
        }
        RunResumeJob(i, move, write_main);
        const bool done = move->status == FATX_MOVE_DONE;
        delete move;
        reload_after_write(m_TableKernel);

        if (done)
        {
            CheckFileSystem(i);
        }
        else
        {
            ShowMessage(L"Not finished", COLOR_RED, L"It stopped again (read or write error). Try again later.");
        }
    }
}

void App::UnmountResized(const bool kept[XboxPartitionTableEntryCount])
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        // Can't unmount our own partition, but nothing writes to it once resized
        if (kept[i] && i != m_LaunchPartition)
        {
            IoDismountVolumeByName(&m_PartitionPaths[i]);
        }
    }
}

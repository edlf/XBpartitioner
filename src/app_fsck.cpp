// The file system check and the safe repairs (src/fatx_check.cpp) on the console.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"
#include "fatx_check.hpp"

// One partition, through the whole disk device from its start sector
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

// Runs on its own thread, so the screen (and the animated background) keeps
// drawing every frame while it waits for the disk. write false: a dry run,
// which is the check plus what the safe repairs would change.
struct FsckJob : public FatxProgress
{
    ULONGLONG start_lba;
    ULONGLONG size_bytes;
    bool write;
    FatxRepairResult *repair;
    volatile LONG percent; // Read by the drawing thread

    void Update(int value)
    {
        InterlockedExchange((LPLONG)&percent, value); // The XDK's takes no volatile pointer
    }
};

static DWORD WINAPI fsck_thread(LPVOID parameter)
{
    FsckJob *job = (FsckJob *)parameter;
    DiskPartitionDevice device(job->start_lba, job->write);
    FatxRepair(device, job->size_bytes, job->repair, job->write, job);
    return 0;
}

// The checker's own stack use is small (no recursion, buffers on the heap)
static const DWORD FSCK_THREAD_STACK = 64 * 1024;

// "Error: \games\x: ..." cut to what fits a dialog line
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

// "1.50 MB" for a number of clusters
static void format_clusters_size(ULONGLONG clusters, uint32_t cluster_bytes, wchar_t *out)
{
    format_human_size(clusters * cluster_bytes / HDD_SECTOR_SIZE, out);
}

void App::RunFsckJob(int partition, bool write, LPCWSTR verb, FatxRepairResult *repair)
{
    wchar_t busy[60];
    swprintf(busy, L"%s partition %d (%c)...", verb, partition + 1, partition_letter(partition));
    const DWORD opened = OpenBusyWindow(busy);

    FsckJob job;
    job.start_lba = m_pTableOriginal->GetPartitionStart(partition);
    job.size_bytes = m_pTableOriginal->GetPartitionSize(partition) * HDD_SECTOR_SIZE;
    job.write = write;
    job.repair = repair;
    job.percent = 0;

    HANDLE thread = CreateThread(NULL, FSCK_THREAD_STACK, fsck_thread, &job, 0, NULL);
    if (thread != NULL)
    {
        // A frame per vertical blank until it's done
        wchar_t progress[60];
        while (WaitForSingleObject(thread, 0) == WAIT_TIMEOUT)
        {
            swprintf(progress, L"%s partition %d (%c)... %d%%", verb, partition + 1, partition_letter(partition),
                     (int)job.percent);
            DrawBusyWindow(progress);
        }
        CloseHandle(thread);
    }
    else
    {
        // No thread: here, the screen just pauses
        fsck_thread(&job);
    }

    CloseBusyWindow(busy, opened);
}

void App::DoCheckFileSystem()
{
    CheckFileSystem(m_CurrentPart);
}

// The partition as it is on disk (m_pTableOriginal), not as edited
void App::CheckFileSystem(int partition)
{
    if (!m_pTableOriginal->isPartitionEnabled(partition))
    {
        ShowMessage(L"Not checked", COLOR_YELLOW, L"This partition isn't in the table on disk yet.");
        return;
    }

    // ~10 KB: not on the 64 KB stack
    FatxRepairResult *plan = new FatxRepairResult;
    if (plan == NULL)
    {
        ShowMessage(L"Not checked", COLOR_YELLOW, L"Not enough memory for the check.");
        return;
    }
    RunFsckJob(partition, false, L"Checking", plan);

    // Something the safe repairs fix (they only run without errors)
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

// True if the user picked Repair (only offered with offer_repair)
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
            lines[0] = line_counts; // No files were read
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

    // The first few, errors first
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
            // The lost space line says it already
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

// The safe repairs plan found by the dry run: confirm, unmount, repair,
// check again
void App::RepairFileSystem(int partition, const FatxRepairResult &plan)
{
    // Its .xbe and settings are open: never under a running program
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

    // The kernel's cached FAT and directories would overwrite the repair
    const NTSTATUS dismounted = IoDismountVolumeByName(&m_PartitionPaths[partition]);
    if (!NT_SUCCESS(dismounted))
    {
        wchar_t error[80];
        swprintf(error, L"Unmounting failed (0x%08X): nothing was changed.", dismounted);
        ShowMessage(L"Not repaired", COLOR_RED, error);
        return;
    }

    // Checks again before writing, and only writes without errors
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

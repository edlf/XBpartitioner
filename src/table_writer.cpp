#include "stdafx.h"
#include "table_writer.h"
#include "array_length.h"
#include "disk_partition_device.h"
#include "partition_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Writes a resized partition's new size to the table. Runs on the resize's
// worker thread, so no dialogs: write_main is decided beforehand.
class XboxResizeTable : public FatxResizeTable
{
  public:
    XboxResizeTable(TableWriter *writer, int partition, const XboxPartitionTable &base, bool write_main)
        : m_writer(writer), m_partition(partition), m_base(base), m_write_main(write_main)
    {
    }

    bool SetSize(uint64_t new_lba_size)
    {
        return m_writer->WriteTablesRaw(TableWith(new_lba_size), m_write_main).written;
    }

    XboxPartitionTable TableWith(uint64_t new_lba_size)
    {
        // Sizes here are already aligned
        PartitionTableEditor table = m_writer->m_tables.Manipulate(m_base);
        table.SetPartitionSize(m_partition, new_lba_size);
        return table.GetPartitionTable();
    }

  private:
    TableWriter *m_writer;
    int m_partition;
    XboxPartitionTable m_base;
    bool m_write_main;
};

TableWriter::TableWriter(const TableWriterParts &parts)
    : m_tables(parts.tables), m_session(parts.session), m_filesystems(parts.filesystems), m_fsck(parts.fsck), m_dialogs(parts.dialogs),
      m_settings(parts.settings), m_launch_partition(parts.launch_partition), m_settings_save_blocked(false)
{
}

bool TableWriter::StartFromKernelTable()
{
    const bool patched = m_tables.ReadKernelTable(m_settings);
    m_session.Start(patched ? m_tables.Manipulate(m_tables.KernelTable()) : m_tables.StockLayout());
    return patched;
}

// Lists what will be formatted and calls out E and C. True to go ahead.
// resizes: what the edited table resizes without formatting.
bool TableWriter::ConfirmFormatWarning(const PartitionSet &marked, const ResizesKeepingData &resizes)
{
    wchar_t list[100];
    FormatPartitionList(marked, list, ArrayLength(list));
    wchar_t message[120];
    if (list[0] == L'\0')
    {
        swprintf(message, L"No partitions changed, nothing will be formatted.");
    }
    else
    {
        swprintf(message, L"Format: %s", list);
    }

    LPCWSTR lines[7];
    int line_count = 0;
    lines[line_count++] = message;
    ResizeNotes notes;
    line_count += AddResizeNotes(resizes, &notes, lines + line_count);
    if (resizes.reason[0] != L'\0')
    {
        lines[line_count++] = resizes.reason;
    }

    // Partition 0 is E, partition 1 is C
    if (marked.Contains(0))
    {
        lines[line_count++] = L"E holds saves and settings!";
    }
    if (marked.Contains(1))
    {
        lines[line_count++] = L"C holds the dashboard, the Xbox may not boot!";
    }

    // Removed in the new table: not formatted, but gone all the same
    wchar_t program_line[100];
    if (marked.Contains(m_launch_partition) || LosesLaunchPartition(resizes.Keeping()))
    {
        FormatLaunchPartitionLine(program_line, marked.Contains(m_launch_partition) ? L"it will be deleted." : L"it will be unreadable.");
        lines[line_count++] = program_line;
    }
    return m_dialogs.ConfirmWarning(lines, line_count, L"Format");
}

int TableWriter::AddResizeNotes(const ResizesKeepingData &resizes, ResizeNotes *notes, LPCWSTR *lines)
{
    int added = 0;
    if (FormatKeptLine(resizes.Keeping(), notes->kept, ArrayLength(notes->kept)))
    {
        lines[added++] = notes->kept;
    }
    if (FormatMovedLine(resizes.moved, notes->moved, ArrayLength(notes->moved)))
    {
        lines[added++] = notes->moved;
    }
    return added;
}

bool TableWriter::LosesLaunchPartition(const PartitionSet &keeps) const
{
    return MovesLaunchPartition(m_session.Edited().GetPartitionTable()) && !KeepsLaunchPartition(keeps);
}

// Before the first usable sector: in the area the Xbox reserves
void TableWriter::ShowReservedAreaWarning(int partition, LPCWSTR what)
{
    wchar_t line1[100];
    swprintf(line1, L"Partition %d (%c) starts before sector 0x%X,", partition + 1, PartitionLetter(partition), XBOX_SWAPPART1_LBA_START);
    const LPCWSTR lines[] = {
        line1,
        L"in the area the Xbox reserves at the start of the drive.",
        L"Move its start forward or disable it in the editor",
        what,
    };
    m_dialogs.ShowWarning(lines, ArrayLength(lines));
}

bool TableWriter::RefuseUnwritableTable(const XboxPartitionTable &table)
{
    const PartitionTableEditor check = m_tables.Manipulate(table);

    int reserved = 0;
    if (check.FindPartitionBeforeUsableStart(&reserved))
    {
        ShowReservedAreaWarning(reserved, L"before writing.");
        return true;
    }

    int first = 0;
    int second = 0;
    if (!check.FindConflict(&first, &second))
    {
        return false;
    }

    wchar_t line[100];
    swprintf(line,
             L"Partitions %d (%c) and %d (%c) %s.",
             first + 1,
             PartitionLetter(first),
             second + 1,
             PartitionLetter(second),
             (check.GetPartitionStart(first) == check.GetPartitionStart(second)) ? L"start at the same sector" : L"overlap");
    const LPCWSTR lines[] = {
        line,
        L"Fix that in the partition editor before writing.",
    };
    m_dialogs.ShowWarning(lines, ArrayLength(lines));
    return true;
}

void TableWriter::WriteAndFormatChanged()
{
    if (RefuseUnwritableTable(m_session.Edited().GetPartitionTable()))
    {
        return;
    }

    // Partitions that keep their data aren't formatted. Before writing: the
    // write makes the new table the baseline.
    const ResizesKeepingData resizes = FindResizesKeepingData();
    const PartitionSet to_format = m_session.Changes().Changed(resizes.Keeping());

    if (!ConfirmFormatWarning(to_format, resizes))
    {
        return;
    }

    const TableWriteOutcome outcome = WriteTableWithResizes(resizes);
    if (!outcome.written)
    {
        if (!outcome.error_shown)
        {
            m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
        }
        return;
    }

    // Without the patch the kernel keeps the old layout until a reboot
    if (!m_tables.HasLba48Patch())
    {
        m_dialogs.ShowMessage(L"Not formatted", COLOR_YELLOW, L"Table written. Formatting needs an LBA48 BIOS.");
        return;
    }
    FormatAfterWrite(to_format, outcome);
}

void TableWriter::FormatAfterWrite(const PartitionSet &to_format, const TableWriteOutcome &outcome)
{
    int attempted = 0;
    int formatted = 0;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (to_format.Contains(i))
        {
            attempted++;
            if (FormatPartition(i))
            {
                formatted++;
            }
        }
    }

    wchar_t done[60];
    swprintf(done, L"Table written, %d of %d partitions formatted.", formatted, attempted);
    const LPCWSTR lines[] = {
        done,
        L"No backup: the last partition covers its sector.",
    };
    const bool ok = (formatted == attempted);
    const bool backup_explained = outcome.backup_written || m_session.Edited().GetBackupTableLba() == 0 || !m_tables.HasLba48Patch();
    const DialogText text = {ok ? L"Done" : L"Finished with errors", ok ? COLOR_GREEN : COLOR_YELLOW, lines, backup_explained ? 1 : 2};
    m_dialogs.RunDialog(text, DIALOG_OK_ONLY);
}

void TableWriter::WriteEditedTable()
{
    if (RefuseUnwritableTable(m_session.Edited().GetPartitionTable()))
    {
        return;
    }

    // Moving or resizing makes a partition unreadable, unless the resize can
    // keep its data
    const ResizesKeepingData resizes = FindResizesKeepingData();
    if (ConfirmWrite(resizes))
    {
        ReportWrite(WriteTableWithResizes(resizes));
    }
}

bool TableWriter::ConfirmWrite(const ResizesKeepingData &resizes)
{
    const PartitionSet resized = m_session.Changes().MovedOrResized(resizes.Keeping());
    if (!resized.IsEmpty() || LosesLaunchPartition(resizes.Keeping()))
    {
        return ConfirmUnreadable(resizes, resized);
    }
    return ConfirmPlainWrite(resizes);
}

bool TableWriter::ConfirmUnreadable(const ResizesKeepingData &resizes, const PartitionSet &resized)
{
    wchar_t list[100];
    FormatPartitionList(resized, list, ArrayLength(list));
    wchar_t resized_line[120];
    swprintf(resized_line, L"Moved or resized: %s", list);

    LPCWSTR lines[7];
    int line_count = 0;
    if (!resized.IsEmpty())
    {
        lines[line_count++] = resized_line;
        lines[line_count++] = L"Their data is unreadable until they are formatted.";
        lines[line_count++] = L"Use Start > Partition table > Write table and format.";
    }
    if (resizes.reason[0] != L'\0')
    {
        lines[line_count++] = resizes.reason;
    }
    ResizeNotes notes;
    line_count += AddResizeNotes(resizes, &notes, lines + line_count);

    wchar_t program_line[100];
    if (LosesLaunchPartition(resizes.Keeping()))
    {
        FormatLaunchPartitionLine(program_line, L"it will be unreadable.");
        lines[line_count++] = program_line;
    }
    return m_dialogs.ConfirmWarning(lines, line_count, L"Write anyway");
}

bool TableWriter::ConfirmPlainWrite(const ResizesKeepingData &resizes)
{
    LPCWSTR lines[3];
    int line_count = 0;
    lines[line_count++] = L"The partition table will be written to the hard drive.";
    ResizeNotes notes;
    line_count += AddResizeNotes(resizes, &notes, lines + line_count);
    return m_dialogs.RunDialog(DialogText{L"Write table", COLOR_WHITE, lines, line_count}, DialogButtons{L"Write", L"Cancel"});
}

void TableWriter::ReportWrite(const TableWriteOutcome &outcome)
{
    if (!outcome.written)
    {
        if (!outcome.error_shown)
        {
            m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
        }
        return;
    }

    LPCWSTR result = L"The partition table was written.";
    if (!outcome.main_written)
    {
        result = L"Written to the backup table only (Titan BIOS).";
    }
    else if (!m_tables.HasLba48Patch())
    {
        result = L"Table written (no backup without an LBA48 BIOS).";
    }
    else if (!outcome.backup_written && m_session.Edited().GetBackupTableLba() != 0)
    {
        result = L"Table written, no backup: the last partition covers it.";
    }
    m_dialogs.ShowMessage(L"Done", COLOR_GREEN, result);
}

void TableWriter::FormatOne(int part)
{
    // Only the written layout: formatting an unwritten one would store data
    // where the table on disk (after a reboot) has no partition
    if (m_session.Changes().Any())
    {
        m_dialogs.ShowMessage(L"Not formatted", COLOR_YELLOW, L"Unsaved changes: write the table first.");
        return;
    }

    if (m_session.InUse().IsPartitionEnabled(part) && m_session.InUse().GetPartitionStart(part) < XBOX_SWAPPART1_LBA_START)
    {
        ShowReservedAreaWarning(part, L"and write the table before formatting.");
        return;
    }

    if (!ConfirmFormatWarning(PartitionSet::Only(part), ResizesKeepingData()))
    {
        return;
    }

    if (FormatPartition(part))
    {
        wchar_t done[40];
        swprintf(done, L"Partition %d was formatted.", part + 1);
        m_dialogs.ShowMessage(L"Done", COLOR_GREEN, done);
    }
}

void TableWriter::FormatLaunchPartitionLine(wchar_t *out, LPCWSTR what)
{
    swprintf(out, L"Partition %d (%c) holds this program: %s", m_launch_partition + 1, PartitionLetter(m_launch_partition), what);
}

bool TableWriter::MovesLaunchPartition(const XboxPartitionTable &table) const
{
    const PartitionTableEditor written = m_tables.Manipulate(table);
    return PartitionTableChanges(written, m_session.InUse()).Affects(m_launch_partition);
}

// Writes the stock layout (main and backup table)
void TableWriter::EraseTables()
{
    static const LPCWSTR lines[] = {
        L"The partition table on the hard drive will be replaced",
        L"by the stock layout. Extended partitions are lost.",
    };
    if (!m_dialogs.ConfirmWarning(lines, ArrayLength(lines), L"Erase"))
    {
        return;
    }

    if (ErasePartitionTables())
    {
        m_dialogs.ShowMessage(L"Done", COLOR_GREEN, L"The partition table was replaced by the stock layout.");
    }
    else
    {
        m_dialogs.ShowMessage(L"Error", COLOR_RED, L"The partition table could not be erased.");
    }
}

bool TableWriter::FormatPartition(const int i)
{
    // Last line of defence, the callers explain it first.
    // The table in use is where the kernel has the partition.
    if (m_session.InUse().GetPartitionStart(i) < XBOX_SWAPPART1_LBA_START)
    {
        m_dialogs.ShowMessage(L"Error", COLOR_RED, L"Not formatted: it starts in the reserved area.");
        return false;
    }

    // From the partition's size on disk, the one that's formatted (any size,
    // stock or not, for every partition). v3 kernels only mount clusters up
    // to 64 KB.
    const int cluster_size = m_session.InUse().CalculateClusterSizeForPartition(i);
    const unsigned int max_cluster = m_session.Edited().GetMaxClusterSize();
    if (max_cluster != 0 && (unsigned int)cluster_size > max_cluster)
    {
        wchar_t error[80];
        swprintf(error, L"Partition %d needs %d KB clusters, LBA48 v3 allows %u.", i + 1, cluster_size, max_cluster);
        const LPCWSTR lines[] = {
            error,
            L"Not formatted: shrink it to 1 TB or less first.",
        };
        m_dialogs.RunDialog(DialogText{L"Error", COLOR_RED, lines, ArrayLength(lines)}, DIALOG_OK_ONLY);
        return false;
    }

    // D: may be on this partition
    if (m_launch_partition < 0 || i == m_launch_partition)
    {
        m_settings_save_blocked = true;
    }

    // The kernel's cached view of the old file system must not write over the new one
    const NTSTATUS dismounted = DismountPartition(i);
    if (!NT_SUCCESS(dismounted))
    {
        wchar_t error[80];
        swprintf(error, L"Partition %d: unmounting failed (0x%08X), not formatted.", i + 1, dismounted);
        m_dialogs.ShowMessage(L"Error", COLOR_RED, error);
        return false;
    }

    const bool formatted = m_fsck.Format(i, (uint32_t)cluster_size << 10);

    // The header changed (or the old one is wiped): re-read it
    m_filesystems.Refresh(i);

    if (!formatted)
    {
        wchar_t error[80];
        swprintf(error, L"Partition %d could not be formatted (write error).", i + 1);
        m_dialogs.ShowMessage(L"Error", COLOR_RED, error);
        return false;
    }
    return true;
}

// "Resized, data kept: 6 (F)"; false if there are none
bool TableWriter::FormatKeptLine(const PartitionSet &kept, wchar_t *out, size_t out_count)
{
    wchar_t list[100];
    FormatPartitionList(kept, list, ArrayLength(list));
    if (list[0] == L'\0')
    {
        return false;
    }
    _snwprintf(out, out_count, L"Resized, data kept: %s", list);
    out[out_count - 1] = L'\0';
    return true;
}

// "Clusters move in: 6 (F)"; false if there are none
bool TableWriter::FormatMovedLine(const PartitionSet &moved, wchar_t *out, size_t out_count)
{
    wchar_t list[100];
    FormatPartitionList(moved, list, ArrayLength(list));
    if (list[0] == L'\0')
    {
        return false;
    }
    _snwprintf(out, out_count, L"Clusters move in: %s.", list);
    out[out_count - 1] = L'\0';
    return true;
}

// Whether our own partition is resized but keeps its data
bool TableWriter::KeepsLaunchPartition(const PartitionSet &kept) const
{
    return kept.Contains(m_launch_partition);
}

// Like PartitionTableStore::Prepare(), but a Titan BIOS only reads the backup,
// so ask whether sector 0 should be written too (for Cerbios)
TableWritePlan TableWriter::PlanTableWrite(const XboxPartitionTable &table_in)
{
    TableWritePlan plan = m_tables.Prepare(table_in);
    if (!plan.writable || !m_tables.TitanBios())
    {
        return plan;
    }
    static const LPCWSTR lines[] = {
        L"Titan patched BIOSes only use the backup table.",
        L"Also write it to sector 0, for compatibility with",
        L"Cerbios and other BIOSes that use the table there?",
    };
    plan.write_main = m_dialogs.RunDialog(DialogText{L"Titan BIOS", COLOR_WHITE, lines, ArrayLength(lines)}, DialogButtons{L"Sector 0 too", L"Backup only"});
    plan.writable = plan.backup_possible || plan.write_main;
    return plan;
}

// Writes the tables to disk and to the kernel, without dialogs or editor
// updates, so a worker thread can call it. Reload the editor afterwards on
// the main thread.
TableWriteOutcome TableWriter::WriteTablesRaw(const XboxPartitionTable &table_in, bool write_main)
{
    // From the moment it is written D: may no longer match its partition
    const TableWritePlan plan = m_tables.Prepare(table_in);
    const bool writes = plan.writable && (plan.backup_possible || write_main);
    if (writes && (m_launch_partition < 0 || MovesLaunchPartition(plan.table)))
    {
        m_settings_save_blocked = true;
    }
    return m_tables.Write(table_in, write_main);
}

// Refreshes the editor, the on-disk tables and the cluster sizes after a write
void TableWriter::ReloadAfterWrite(const XboxPartitionTable &written)
{
    if (m_tables.HasLba48Patch())
    {
        // Rebuilds both editor tables from the updated kernel table
        StartFromKernelTable();
    }
    else
    {
        // No patch: StartFromKernelTable() would fail, so the written table becomes the
        // baseline here. The kernel only sees it after a reboot.
        m_session.Restart(written);
    }

    // After the kernel's table: it decides which sectors the backup may be in
    m_tables.ReadDiskTables();

    // Partitions may now point at different sectors
    m_filesystems.RefreshAll();
}

TableWriteOutcome TableWriter::WriteTables(const XboxPartitionTable &table_in)
{
    const TableWritePlan plan = PlanTableWrite(table_in);
    if (!plan.writable)
    {
        return TableWriteOutcome::NotWritten();
    }

    const TableWriteOutcome outcome = WriteTablesRaw(plan.table, plan.write_main);
    if (outcome.written)
    {
        ReloadAfterWrite(plan.table);
    }
    return outcome;
}

// Built on a copy, so the edits survive a failed write
bool TableWriter::ErasePartitionTables()
{
    PartitionTableEditor stock = m_tables.Manipulate(m_session.Edited().GetPartitionTable());
    stock.SetupPartitionsStock();
    return WriteTables(stock.GetPartitionTable()).written;
}

// Finds resized partitions that can keep their data, either by a table change
// (kept) or by moving clusters (moved)
ResizesKeepingData TableWriter::FindResizesKeepingData()
{
    ResizesKeepingData resizes;
    // Off: every resized partition is formatted, nothing is read or checked
    if (!m_settings.ResizeKeepsData())
    {
        return resizes;
    }
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!m_session.Changes().IsResizedAtSameStart(i) || !m_filesystems.EnsureUsage(i))
        {
            continue; // Moved, unchanged, or nothing to keep
        }
        const ULONGLONG old_size = m_session.InUse().GetPartitionSize(i);
        const ULONGLONG new_size = m_session.Edited().GetPartitionSize(i);

        // By the usage first, the same as the editor; the disk has the last word
        const FatxResizeKind kind = FatxClassifyResize(old_size, m_filesystems.Usage(i).Get(), new_size);
        if (kind == FATX_RESIZE_KIND_FORMAT)
        {
            continue;
        }
        const bool in_place = (kind == FATX_RESIZE_KIND_IN_PLACE);
        const LPCWSTR why = in_place ? InPlaceResizeProblem(i, new_size) : MoveResizeProblem(i, new_size);
        if (why == NULL)
        {
            (in_place ? resizes.kept : resizes.moved).Add(i);
        }
        else if (why[0] != L'\0' && resizes.reason[0] == L'\0')
        {
            FormatWide(resizes.reason, ArrayLength(resizes.reason), L"%d (%c) can't keep its data: %s.", i + 1, PartitionLetter(i), why);
        }
    }
    return resizes;
}

LPCWSTR TableWriter::InPlaceResizeProblem(int partition, ULONGLONG new_size)
{
    FatxResizeResult *resize = new FatxResizeResult;
    if (resize == NULL)
    {
        return L"";
    }
    m_fsck.RunResizeCheck(partition, new_size, resize);
    const FatxResizeStatus status = resize->status;
    delete resize;

    switch (status)
    {
    case FATX_RESIZE_OK:
        return NULL;
    case FATX_RESIZE_CLUSTERS_IN_USE:
        return L"the space it gives up is in use";
    case FATX_RESIZE_HAS_ERRORS:
        return L"its file system has errors";
    case FATX_RESIZE_FAT_NOT_CLEAN:
        return L"its FAT isn't clean past its end";
    default:
        return L"it could not be checked";
    }
}

// It gets unmounted, so not the partition we're running from
LPCWSTR TableWriter::MoveResizeProblem(int partition, ULONGLONG new_size)
{
    FatxMoveResult *move = (partition == m_launch_partition) ? NULL : new FatxMoveResult;
    if (move == NULL)
    {
        return L"this program runs from it";
    }
    m_fsck.PlanMoveResize(partition, new_size, move);
    const FatxMoveStatus status = move->status;
    delete move;

    switch (status)
    {
    case FATX_MOVE_PLANNED:
        return NULL;
    case FATX_MOVE_NO_ROOM:
        return L"not enough free space to move its clusters";
    case FATX_MOVE_HAS_ERRORS:
        return L"its file system has errors";
    default:
        return L"it could not be checked";
    }
}

// Runs these resizes with moves. Each partition is unmounted first and
// writes its own size to the table. Stops at the first failure and explains it.
bool TableWriter::RunMoveResizes(const PartitionSet &partitions, const ULONGLONG new_sizes[XboxPartitionTableEntryCount], bool write_main)
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (partitions.Contains(i) && !RunMoveResize(i, new_sizes[i], write_main))
        {
            return false;
        }
    }
    return true;
}

bool TableWriter::RunMoveResize(int partition, ULONGLONG new_size, bool write_main)
{
    // Make sure the new size fits the current table
    XboxResizeTable check(this, partition, m_tables.KernelTable(), write_main);
    if (!m_tables.Prepare(check.TableWith(new_size)).writable)
    {
        wchar_t line[80];
        swprintf(line, L"Partition %d (%c) wasn't resized: it doesn't fit.", partition + 1, PartitionLetter(partition));
        m_dialogs.ShowMessage(L"Not resized", COLOR_RED, line);
        return false;
    }

    DismountPartition(partition);
    FatxMoveResult *move = new FatxMoveResult;
    if (move == NULL)
    {
        return false;
    }
    XboxResizeTable table(this, partition, m_tables.KernelTable(), write_main);
    const MoveResizeRequest request = {partition, new_size, &table};
    m_fsck.RunMoveResize(request, move);

    // The worker thread changed the kernel's table; reload the editor
    ReloadAfterWrite(m_tables.KernelTable());

    const bool done = (move->status == FATX_MOVE_DONE);
    if (!done)
    {
        ExplainStoppedResize(partition, *move);
    }
    delete move;
    return done;
}

void TableWriter::ExplainStoppedResize(int partition, const FatxMoveResult &move)
{
    const int number = partition + 1;
    const wchar_t letter = PartitionLetter(partition);
    wchar_t line1[80];
    wchar_t line2[80];
    LPCWSTR lines[3];
    int line_count = 0;
    if (move.status == FATX_MOVE_STOPPED_SAFE)
    {
        swprintf(line1, L"Resizing partition %d (%c) stopped while moving", number, letter);
        lines[line_count++] = line1;
        lines[line_count++] = L"clusters: it keeps its old size and its data.";
        lines[line_count++] = L"A check may show lost space, which Repair frees.";
    }
    else if (move.status == FATX_MOVE_INTERRUPTED && move.table_failed)
    {
        swprintf(line1, L"Partition %d (%c) was resized, but the partition", number, letter);
        lines[line_count++] = line1;
        lines[line_count++] = L"table couldn't be written. Finish it: it's offered";
        lines[line_count++] = L"at the next start. Until then it can't be used.";
    }
    else if (move.status == FATX_MOVE_INTERRUPTED)
    {
        swprintf(line1, L"Resizing partition %d (%c) stopped partway (%I64u", number, letter, move.clusters_moved);
        swprintf(line2, L"clusters moved). Finish it: it's offered at the next start.");
        lines[line_count++] = line1;
        lines[line_count++] = line2;
        lines[line_count++] = L"Until then it can't be used.";
    }
    else
    {
        swprintf(line1, L"Partition %d (%c) couldn't be resized: nothing", number, letter);
        lines[line_count++] = line1;
        lines[line_count++] = L"was changed on it (check it for errors).";
    }
    m_dialogs.RunDialog(DialogText{L"Not resized", COLOR_RED, lines, line_count}, DIALOG_OK_ONLY);
}

// Writes the edited table, keeping the data of resized partitions. Shrinks
// with moves go first so their space is free, growths with moves go last,
// and everything else is written in between.
TableWriteOutcome TableWriter::WriteTableWithResizes(const ResizesKeepingData &resizes)
{
    const PartitionSet &moved = resizes.moved;
    const XboxPartitionTable edited = m_session.Edited().GetPartitionTable();
    ULONGLONG new_sizes[XboxPartitionTableEntryCount];
    const bool any_moved = !moved.IsEmpty();
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        new_sizes[i] = m_session.Edited().GetPartitionSize(i);
    }

    // Ask the Titan BIOS question once, up front
    bool write_main = true;
    if (any_moved)
    {
        const TableWritePlan plan = PlanTableWrite(edited);
        if (!plan.writable)
        {
            return TableWriteOutcome::NotWritten();
        }
        write_main = plan.write_main;
        const LPCWSTR lines[] = {
            L"Partition will be resized now.",
            L"DON'T switch the Xbox off until it's done.",
        };
        m_dialogs.ShowWarning(lines, ArrayLength(lines));
    }

    // Both from the sizes on disk before anything is written
    const PartitionSet shrinking = m_session.Changes().Shrinking(moved);
    const PartitionSet growing = m_session.Changes().Growing(moved);
    if (!RunMoveResizes(shrinking, new_sizes, write_main))
    {
        return TableWriteOutcome::ErrorShown();
    }

    // Everything else; partitions still to grow keep their old size for now
    PartitionTableEditor rest = m_tables.Manipulate(edited);
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (growing.Contains(i))
        {
            rest.SetPartitionSize(i, m_session.InUse().GetPartitionSize(i));
        }
    }
    const TableWriteOutcome outcome = WriteTables(rest.GetPartitionTable());
    if (!outcome.written)
    {
        return outcome;
    }
    UnmountResized(resizes.kept);

    if (!RunMoveResizes(growing, new_sizes, write_main))
    {
        return TableWriteOutcome::ErrorShown();
    }
    return outcome;
}

// At startup: offers to finish interrupted resizes. Until then the
// partition is unusable (its signature reads "XRSZ").
void TableWriter::OfferFinishResizes()
{
    if (!m_tables.HasLba48Patch())
    {
        return;
    }
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (m_session.InUse().IsPartitionEnabled(i))
        {
            OfferFinishResize(i);
        }
    }
}

// The XRSZ signature without a journal: a cluster size change; with one: a resize
TableWriter::InterruptedResize TableWriter::FindInterruptedResize(int partition)
{
    DiskPartitionDevice device(m_session.InUse().GetPartitionStart(partition), false);
    unsigned char sector[HDD_SECTOR_SIZE];
    uint32_t signature = 0;
    if (device.Read(0, sector, sizeof(sector)))
    {
        memcpy(&signature, sector, 4);
    }

    InterruptedResize found = {false, false, 0};
    uint64_t old_size;
    found.resizing = FatxFindInterruptedResize(device, &old_size, &found.new_size);
    found.reclustering = !found.resizing && signature == FATX_RESIZING_SIGNATURE;
    return found;
}

void TableWriter::OfferFinishResize(int partition)
{
    const InterruptedResize found = FindInterruptedResize(partition);
    wchar_t line1[80];
    if (found.reclustering)
    {
        swprintf(line1, L"Changing partition %d (%c)'s cluster size was", partition + 1, PartitionLetter(partition));
        const LPCWSTR lines[] = {
            line1,
            L"interrupted on a PC. Finish it there:",
            L"fatxtool recluster <disk image> --resume.",
        };
        m_dialogs.ShowWarning(lines, ArrayLength(lines));
        return;
    }
    if (!found.resizing)
    {
        return;
    }

    wchar_t new_size_text[HUMAN_SIZE_TEXT_COUNT];
    FormatHumanSize(found.new_size, new_size_text);
    swprintf(line1, L"Resizing partition %d (%c) to %s was interrupted.", partition + 1, PartitionLetter(partition), new_size_text);
    const LPCWSTR lines[] = {
        line1,
        L"It can't be used until that's finished.",
        L"Finish it now? DON'T turn off your XBOX.",
    };
    const DialogText text = {L"Interrupted resize", COLOR_WARNING, lines, ArrayLength(lines)};
    if (partition != m_launch_partition && m_dialogs.RunDialog(text, DialogButtons{L"Finish", L"Later"}))
    {
        FinishResize(partition);
    }
}

void TableWriter::FinishResize(int partition)
{
    const TableWritePlan plan = PlanTableWrite(m_tables.KernelTable());
    if (!plan.writable)
    {
        return;
    }
    DismountPartition(partition);
    FatxMoveResult *move = new FatxMoveResult;
    if (move == NULL)
    {
        return;
    }
    XboxResizeTable table(this, partition, m_tables.KernelTable(), plan.write_main);
    m_fsck.RunResume(partition, table, move);
    const bool done = move->status == FATX_MOVE_DONE;
    delete move;
    ReloadAfterWrite(m_tables.KernelTable());

    if (done)
    {
        m_fsck.CheckFileSystem(partition);
    }
    else
    {
        m_dialogs.ShowMessage(L"Not finished", COLOR_RED, L"It stopped again (read or write error). Try again later.");
    }
}

void TableWriter::UnmountResized(const PartitionSet &kept)
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        // Can't unmount our own partition, but nothing writes to it once resized
        if (kept.Contains(i) && i != m_launch_partition)
        {
            DismountPartition(i);
        }
    }
}

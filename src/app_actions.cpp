// Writing, formatting and erasing, and the checks and warnings before them.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"
#include "prometheos_code.h" // largePartitionFixup

// Lists what will be formatted and calls out E and C. True to go ahead.
bool App::ConfirmFormatWarning(bool format_changed)
{
    bool marked[XboxPartitionTableEntryCount];
    if (format_changed)
    {
        GetChangedPartitions(marked, false);
    }
    else
    {
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            marked[i] = (i == m_CurrentPart);
        }
    }

    wchar_t list[100];
    FormatPartitionList(marked, list, ARRAY_COUNT(list));

    wchar_t message[120];
    if (list[0] == L'\0')
    {
        swprintf(message, L"No partitions changed, nothing will be formatted.");
    }
    else
    {
        swprintf(message, L"Format: %s", list);
    }

    // marked[0] is E, marked[1] is C
    LPCWSTR lines[4];
    int line_count = 0;
    wchar_t program_line[100];
    lines[line_count++] = message;
    if (marked[0])
    {
        lines[line_count++] = L"E holds saves and settings!";
    }
    if (marked[1])
    {
        lines[line_count++] = L"C holds the dashboard, the Xbox may not boot!";
    }
    if (m_LaunchPartition >= 0 && marked[m_LaunchPartition])
    {
        FormatLaunchPartitionLine(program_line, L"it will be deleted.");
        lines[line_count++] = program_line;
    }
    else if (format_changed && MovesLaunchPartition(m_pTable->GetPartitionTable()))
    {
        // Removed in the new table: not formatted, but gone all the same
        FormatLaunchPartitionLine(program_line, L"it will be unreadable.");
        lines[line_count++] = program_line;
    }

    return ConfirmWarning(lines, line_count, L"Format");
}

// Before the first usable sector: in the area the Xbox reserves
void App::ShowReservedAreaWarning(int partition, LPCWSTR what)
{
    wchar_t line1[100];
    swprintf(line1, L"Partition %d (%c) starts before sector 0x%X,", partition + 1, partition_letter(partition),
             XBOX_SWAPPART1_LBA_START);
    const LPCWSTR lines[] = {
        line1,
        L"in the area the Xbox reserves at the start of the drive.",
        L"Move its start forward or disable it in the editor",
        what,
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

bool App::RefuseUnwritableTable(const XboxPartitionTable &table)
{
    const XboxPartitionTableManipulator check(table, m_TotalHddSectors, m_FullLba48, m_Physical4k);

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
    swprintf(line, L"Partitions %d (%c) and %d (%c) %s.", first + 1, partition_letter(first), second + 1,
             partition_letter(second),
             (check.GetPartitionStart(first) == check.GetPartitionStart(second)) ? L"start at the same sector" : L"overlap");
    const LPCWSTR lines[] = {
        line,
        L"Fix that in the partition editor before writing.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
    return true;
}

void App::DoFormatAll()
{
    if (RefuseUnwritableTable(m_pTable->GetPartitionTable()))
    {
        return;
    }

    if (!ConfirmFormatWarning(true))
    {
        return;
    }

    XboxPartitionTable table = m_pTable->GetPartitionTable();

    // Before writing: the write makes the new table the baseline
    bool to_format[XboxPartitionTableEntryCount];
    GetChangedPartitions(to_format, false);

    bool backup_written = false;
    if (!write_partition_tables(table, &backup_written))
    {
        ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
        return;
    }

    // Without the patch the kernel keeps the old layout until a reboot
    if (!m_pKernelTable)
    {
        ShowMessage(L"Not formatted", COLOR_YELLOW, L"Table written. Formatting needs an LBA48 BIOS.");
        return;
    }

    ShowStatus(L"Partition table written...", COLOR_GREEN);
    Sleep(500);

    int attempted = 0;
    int formatted = 0;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (to_format[i])
        {
            wchar_t format_str[50];
            swprintf(format_str, L"Formatting partition %d with %dk clusters...", i + 1, m_pTable->CalculateClusterSizeForPartition(i));
            ShowStatus(format_str, COLOR_GREEN);

            attempted++;
            if (format_partition(i))
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
    RunDialog(ok ? L"Done" : L"Finished with errors", ok ? COLOR_GREEN : COLOR_YELLOW,
              lines, (backup_written || m_pTable->GetBackupTableLba() == 0 || m_pKernelTable == NULL) ? 1 : 2, L"OK", NULL);
}

void App::DoWriteTable()
{
    if (RefuseUnwritableTable(m_pTable->GetPartitionTable()))
    {
        return;
    }

    // FATX derives its layout from the size: moved or resized means unreadable
    bool resized[XboxPartitionTableEntryCount];
    const int resized_count = GetChangedPartitions(resized, true);
    const bool moves_program = MovesLaunchPartition(m_pTable->GetPartitionTable());
    if (resized_count > 0 || moves_program)
    {
        wchar_t list[100];
        FormatPartitionList(resized, list, ARRAY_COUNT(list));

        wchar_t line1[120];
        swprintf(line1, L"Moved or resized: %s", list);
        wchar_t program_line[100];
        FormatLaunchPartitionLine(program_line, L"it will be unreadable.");

        LPCWSTR lines[4];
        int line_count = 0;
        if (resized_count > 0)
        {
            lines[line_count++] = line1;
            lines[line_count++] = L"Their data is unreadable until they are formatted.";
            lines[line_count++] = L"Use Start > Partition table > Write table and format.";
        }
        if (moves_program)
        {
            lines[line_count++] = program_line;
        }
        if (!ConfirmWarning(lines, line_count, L"Write anyway"))
        {
            return;
        }
    }
    else
    {
        static const LPCWSTR lines[] = {
            L"The partition table will be written to the hard drive.",
        };
        if (!RunDialog(L"Write table", COLOR_WHITE, lines, ARRAY_COUNT(lines), L"Write", L"Cancel"))
        {
            return;
        }
    }

    bool backup_written = false;
    if (write_partition_tables(m_pTable->GetPartitionTable(), &backup_written))
    {
        LPCWSTR result = L"The partition table was written.";
        if (m_pKernelTable == NULL)
        {
            result = L"Table written (no backup without an LBA48 BIOS).";
        }
        else if (!backup_written && m_pTable->GetBackupTableLba() != 0)
        {
            result = L"Table written, no backup: the last partition covers it.";
        }
        ShowMessage(L"Done", COLOR_GREEN, result);
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
    }
}

void App::DoFormatOne()
{
    // Only the written layout: formatting an unwritten one would store data
    // where the table on disk (after a reboot) has no partition
    if (CheckIfPartitionTableChanged())
    {
        ShowMessage(L"Not formatted", COLOR_YELLOW, L"Unsaved changes: write the table first.");
        return;
    }

    if (m_pTableOriginal->isPartitionEnabled(m_CurrentPart) &&
        m_pTableOriginal->GetPartitionStart(m_CurrentPart) < XBOX_SWAPPART1_LBA_START)
    {
        ShowReservedAreaWarning(m_CurrentPart, L"and write the table before formatting.");
        return;
    }

    if (!ConfirmFormatWarning(false))
    {
        return;
    }

    wchar_t format_str[50];
    swprintf(format_str, L"Formatting partition %d with %dk clusters...", m_CurrentPart + 1, m_pTable->CalculateClusterSizeForPartition(m_CurrentPart));
    ShowStatus(format_str, COLOR_GREEN);

    if (format_partition(m_CurrentPart))
    {
        wchar_t done[40];
        swprintf(done, L"Partition %d was formatted.", m_CurrentPart + 1);
        ShowMessage(L"Done", COLOR_GREEN, done);
    }
}

void App::FormatLaunchPartitionLine(wchar_t *out, LPCWSTR what)
{
    swprintf(out, L"Partition %d (%c) holds this program: %s", m_LaunchPartition + 1,
             partition_letter(m_LaunchPartition), what);
}

bool App::MovesLaunchPartition(const XboxPartitionTable &table) const
{
    if (m_LaunchPartition < 0 || !m_pTableOriginal->isPartitionEnabled(m_LaunchPartition))
    {
        return false;
    }

    const XboxPartitionTableManipulator written(table, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    return !written.isPartitionEnabled(m_LaunchPartition) ||
           written.GetPartitionStart(m_LaunchPartition) != m_pTableOriginal->GetPartitionStart(m_LaunchPartition) ||
           written.GetPartitionSize(m_LaunchPartition) != m_pTableOriginal->GetPartitionSize(m_LaunchPartition);
}

// Writes the stock layout (main and backup table)
void App::DoEraseTable()
{
    static const LPCWSTR lines[] = {
        L"The partition table on the hard drive will be replaced",
        L"by the stock layout. Extended partitions are lost.",
    };
    if (!ConfirmWarning(lines, ARRAY_COUNT(lines), L"Erase"))
    {
        return;
    }

    if (erase_partition_tables())
    {
        ShowMessage(L"Done", COLOR_GREEN, L"The partition table was replaced by the stock layout.");
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The partition table could not be erased.");
    }
}

bool App::format_partition(const int i)
{
    // Last line of defence, the callers explain it first.
    // m_pTableOriginal is where the kernel has the partition.
    if (m_pTableOriginal->GetPartitionStart(i) < XBOX_SWAPPART1_LBA_START)
    {
        ShowMessage(L"Error", COLOR_RED, L"Not formatted: it starts in the reserved area.");
        return false;
    }

    // v3 kernels only mount clusters up to 64 KB
    const int cluster_size = m_pTable->CalculateClusterSizeForPartition(i);
    const unsigned int max_cluster = m_pTable->GetMaxClusterSize();
    if (max_cluster != 0 && (unsigned int)cluster_size > max_cluster)
    {
        wchar_t error[80];
        swprintf(error, L"Partition %d needs %d KB clusters, LBA48 v3 allows %u.", i + 1, cluster_size, max_cluster);
        const LPCWSTR lines[] = {
            error,
            L"Not formatted: shrink it to 1 TB or less first.",
        };
        RunDialog(L"Error", COLOR_RED, lines, ARRAY_COUNT(lines), L"OK", NULL);
        return false;
    }

    // D: may be on this partition
    if (m_LaunchPartition < 0 || i == m_LaunchPartition)
    {
        m_ThemeSaveBlocked = true;
    }

    const ULONG cluster_size_bytes = cluster_size << 10;

    // Returns a BOOL, not an NTSTATUS
    const bool formatted = XapiFormatFATVolumeEx(&m_PartitionPaths[i], cluster_size_bytes) != FALSE;
    const bool fixed_up = formatted && largePartitionFixup(&m_PartitionPaths[i], cluster_size_bytes);

    // The header changed (or is half written): re-read it
    m_ClusterSizes[i] = get_cluster_size_from_partition(m_PartitionPaths[i]);

    if (!formatted || !fixed_up)
    {
        wchar_t error[60];
        if (!formatted)
        {
            swprintf(error, L"Partition %d could not be formatted.", i + 1);
        }
        else
        {
            swprintf(error, L"Partition %d: setting the cluster size failed.", i + 1);
        }
        ShowMessage(L"Error", COLOR_RED, error);
        return false;
    }

    return true;
}

// Enabled, disabled, moved or resized since the last written table
bool App::CheckIfPartitionTableChanged()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const bool enabled = m_pTable->isPartitionEnabled(i);
        if (enabled != m_pTableOriginal->isPartitionEnabled(i))
        {
            return true;
        }

        if (enabled && (m_pTable->GetPartitionStart(i) != m_pTableOriginal->GetPartitionStart(i) ||
                        m_pTable->GetPartitionSize(i) != m_pTableOriginal->GetPartitionSize(i)))
        {
            return true;
        }
    }
    return false;
}

// Marks partitions enabled now whose start or size changed since the last
// write; existing_only: only those enabled before. Returns the count.
int App::GetChangedPartitions(bool changed[XboxPartitionTableEntryCount], bool existing_only)
{
    int count = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const bool was_enabled = m_pTableOriginal->isPartitionEnabled(i);
        changed[i] = m_pTable->isPartitionEnabled(i) &&
                     (!existing_only || was_enabled) &&
                     (!was_enabled ||
                      m_pTable->GetPartitionStart(i) != m_pTableOriginal->GetPartitionStart(i) ||
                      m_pTable->GetPartitionSize(i) != m_pTableOriginal->GetPartitionSize(i));
        if (changed[i])
        {
            count++;
        }
    }

    return count;
}

// "1 (E), 2 (C), 6 (F)" for the marked partitions
void App::FormatPartitionList(const bool marked[XboxPartitionTableEntryCount], wchar_t *out, size_t out_count)
{
    out[0] = L'\0';
    size_t used = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!marked[i])
        {
            continue;
        }

        wchar_t item[16];
        swprintf(item, L"%s%d (%c)", used ? L", " : L"", i + 1, partition_letter(i));

        const size_t item_length = wcslen(item);
        if (used + item_length + 1 > out_count)
        {
            break;
        }
        wcscat(out, item);
        used += item_length;
    }
}

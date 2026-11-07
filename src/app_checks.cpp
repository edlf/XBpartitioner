// Startup checks and the warnings they lead to.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

void App::RunStartupChecks()
{
    // Cancel quits (QuitToDashboard doesn't return)
    if (m_pKernelTable == NULL && !DisplayNoInfo())
    {
        QuitToDashboard();
    }

    // The overview first, the specific warnings after it
    if (SystemInfoNeedsAttention())
    {
        DisplaySystemInfo();
    }

    // Before anything offers to write
    CheckForV4TableOnOldBios();

    // On an older BIOS a restored v4 backup would be used at the wrong place
    const bool backup_needs_v4 = !m_FullLba48 && XboxPartitionTableManipulator::HasHighLbaBits(m_TableHddBackup);
    if (!IsMainTableUsable() && IsBackupTableUsable() && !backup_needs_v4)
    {
        OfferBackupRestore();
    }

    WarnUnalignedPartitions();
    WarnBackupOverlapped();
    CheckTableStatus();
}

// Without the patch the size is the 28 bit one: ends aren't checked (0)
static ULONGLONG table_check_size(const XboxPartitionTable *kernel_table, ULONGLONG total_sectors)
{
    return (kernel_table != NULL) ? total_sectors : 0;
}

bool App::IsMainTableUsable()
{
    return XboxPartitionTableManipulator::IsTableUsable(m_TableHdd, table_check_size(m_pKernelTable, m_TotalHddSectors));
}

bool App::IsBackupTableUsable()
{
    return XboxPartitionTableManipulator::IsTableUsable(m_TableHddBackup, table_check_size(m_pKernelTable, m_TotalHddSectors));
}

// Main table broken, backup fine: offer to restore it (No leaves the disk alone)
void App::OfferBackupRestore()
{
    const LPCWSTR lines[] = {
        XboxPartitionTableManipulator::HasMagic(m_TableHdd) ? L"The main partition table is damaged." : L"The main partition table is missing.",
        L"The backup partition table is valid.",
        L"Restore it? It is written as the main table.",
    };
    if (!RunDialog(L"Warning", COLOR_WARNING, lines, ARRAY_COUNT(lines), L"Restore", L"No"))
    {
        return;
    }

    // Also gives it to the kernel, so the partitions work right away
    if (write_partition_tables(m_TableHddBackup))
    {
        ShowMessage(L"Done", COLOR_GREEN, L"The backup partition table was restored.");
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The backup partition table could not be restored.");
    }
}

// Starts or sizes that aren't whole 4 KB blocks (other tools): editing
// one rounds it, which moves it
void App::WarnUnalignedPartitions()
{
    bool marked[XboxPartitionTableEntryCount];
    bool any = false;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        marked[i] = m_pTable->isPartitionEnabled(i) &&
                    (!m_pTable->isAlignedTo4k(m_pTable->GetPartitionStart(i)) ||
                     !m_pTable->isAlignedTo4k(m_pTable->GetPartitionSize(i)));
        any = any || marked[i];
    }
    if (!any)
    {
        return;
    }

    wchar_t list[100];
    FormatPartitionList(marked, list, ARRAY_COUNT(list));
    wchar_t line1[120];
    swprintf(line1, L"Not aligned to 4 KB: %s.", list);

    const LPCWSTR lines[] = {
        line1,
        m_Physical4k ? L"This drive has 4K sectors, so they are slower."
                     : L"That makes them slower on drives with 4K sectors.",
        L"Editing one rounds it to 4 KB, which moves it:",
        L"it then needs formatting, and its data is lost.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

// Pre v4 patches only use the low 32 bits of each LBA: a table with v4
// values (over 2TB) shows those partitions in the wrong place
void App::CheckForV4TableOnOldBios()
{
    if (m_FullLba48 || m_pKernelTable == NULL)
    {
        return;
    }

    wchar_t found_in[40] = L"";
    const struct
    {
        const XboxPartitionTable *table;
        LPCWSTR name;
    } tables[] = {
        {&m_TableKernel, L"kernel"},
        {&m_TableHdd, L"hdd"},
        {&m_TableHddBackup, L"hdd backup"},
    };

    for (int i = 0; i < ARRAY_COUNT(tables); i++)
    {
        if (XboxPartitionTableManipulator::HasMagic(*tables[i].table) &&
            XboxPartitionTableManipulator::HasHighLbaBits(*tables[i].table))
        {
            if (found_in[0] != L'\0')
            {
                wcscat(found_in, L", ");
            }
            wcscat(found_in, tables[i].name);
        }
    }

    if (found_in[0] == L'\0')
    {
        return;
    }

    // force_lba48_v3 on a v4 BIOS: worded for that
    const bool forced = m_ForceLba48V3 && m_Lba48Version > 3;

    wchar_t line1[80];
    wchar_t line3[80];
    if (forced)
    {
        swprintf(line1, L"LBA48 v3 is forced in the settings, but a partition table");
    }
    else
    {
        swprintf(line1, L"This BIOS has LBA48 patch v%d, but a partition table", m_Lba48Version);
    }
    swprintf(line3, L"Found in: %s.", found_in);

    const LPCWSTR lines[] = {
        line1,
        L"uses v4 values (over 2TB), likely from a v4 BIOS.",
        line3,
        L"Partitions over 2TB are shown in the wrong place.",
        forced ? L"Do not write or format. Turn force_lba48_v3 off."
               : L"Do not write or format. Use a v4 BIOS such as Cerbios.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

// True to continue without the LBA48 patch
bool App::DisplayNoInfo()
{
    static const LPCWSTR lines[] = {
        L"No LBA48 patch found: custom partitions will not work",
        L"with this BIOS. Update your BIOS to Cerbios.",
    };
    return RunDialog(L"Warning", COLOR_WARNING, lines, ARRAY_COUNT(lines), L"Continue", L"Quit");
}

void App::CheckTableStatus()
{
    // Only when both are usable: a broken main gets the restore question,
    // and no backup is normal with other tools
    if (IsMainTableUsable() && IsBackupTableUsable() &&
        memcmp(&m_TableHdd, &m_TableHddBackup, sizeof(XboxPartitionTable)) != 0)
    {
        static const LPCWSTR lines[] = {
            L"The main and backup partition tables don't match!",
            L"Check which one is correct before doing anything.",
            L"Compare them via Partition table > Load partition table.",
        };
        ShowWarning(lines, ARRAY_COUNT(lines));
    }
}

// The last partition covers the backup sector: no backup until that changes
void App::WarnBackupOverlapped()
{
    if (m_BackupStatus != BACKUP_OVERLAPPED)
    {
        return;
    }

    static const LPCWSTR lines[] = {
        L"The last partition covers the backup table sector",
        L"(on 4K sector drives it is in the last 4K block).",
        L"No backup is written until the layout leaves room:",
        L"shrink the last partition, or use a new partition table.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

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

    if (CheckStellar()) {
        WarnStellarDetected();
        QuitToDashboard();
    }

    // The overview first, the specific warnings after it
    if (SystemInfoNeedsAttention())
    {
        DisplaySystemInfo();
    }

    // Before anything offers to write
    CheckTableVersions();
    CheckForV4TableOnOldBios();

    // A backup in Titan's version was written for a Titan BIOS (by it, or here
    // with a copy in sector 0): keep writing it that way
    m_TitanBios = IsBackupTableUsable() && m_TableHddBackup.Version == XBOX_TABLE_VERSION_TITAN;

    // On an older BIOS a restored v4 backup would be used at the wrong place.
    // Only a backup (sector 0 empty) may be a Titan BIOS's: asked first.
    const bool backup_needs_v4 = !m_FullLba48 && XboxPartitionTableManipulator::HasHighLbaBits(m_TableHddBackup);
    if (IsBackupTableUsable() && !backup_needs_v4)
    {
        if (!XboxPartitionTableManipulator::HasMagic(m_TableHdd))
        {
            OfferTitanOrRestore();
        }
        else if (!IsMainTableUsable())
        {
            OfferBackupRestore();
        }
    }

    WarnUnalignedPartitions();
    WarnBackupOverlapped();
    CheckTableStatus();
}

// "kernel, hdd" for the tables that match
static void list_tables(const XboxPartitionTable *const *tables, const LPCWSTR *names, int count,
                        bool (*match)(const XboxPartitionTable &), wchar_t *out)
{
    out[0] = L'\0';
    for (int i = 0; i < count; i++)
    {
        if (tables[i] != NULL && match(*tables[i]))
        {
            if (out[0] != L'\0')
            {
                wcscat(out, L", ");
            }
            wcscat(out, names[i]);
        }
    }
}

// A version other than v3, v4 or Titan is only warned about
void App::CheckTableVersions()
{
    const XboxPartitionTable *tables[] = {m_pKernelTable != NULL ? &m_TableKernel : NULL, &m_TableHdd, &m_TableHddBackup};
    static const LPCWSTR names[] = {L"kernel", L"hdd", L"hdd backup"};

    wchar_t found_in[40];
    list_tables(tables, names, ARRAY_COUNT(tables), XboxPartitionTableManipulator::HasUnknownVersion, found_in);
    if (found_in[0] == L'\0')
    {
        return;
    }

    // The first unknown one's version, for the message
    unsigned char version = 0;
    for (int i = ARRAY_COUNT(tables) - 1; i >= 0; i--)
    {
        if (tables[i] != NULL && XboxPartitionTableManipulator::HasUnknownVersion(*tables[i]))
        {
            version = tables[i]->Version;
        }
    }

    wchar_t line1[80];
    wchar_t line2[80];
    swprintf(line1, L"Unknown partition table version 0x%02X.", version);
    swprintf(line2, L"Found in: %s.", found_in);
    const LPCWSTR lines[] = {
        line1,
        line2,
        L"Known are 0x00 (LBA48 v3), 0x01 (v4) and 0x03 (Titan):",
        L"this one may be read wrong. Check it before writing.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

// Sector 0 is empty and the backup is fine. Titan patched BIOSes only use the
// backup table: with one, the backup is just written again (and from then on
// every write asks about sector 0, see write_partition_tables). Otherwise the
// usual restore question.
void App::OfferTitanOrRestore()
{
    static const LPCWSTR lines[] = {
        L"There is no partition table in sector 0, only the",
        L"backup one. Titan patched BIOSes only use the backup.",
        L"Do you have a Titan patched BIOS?",
    };
    if (!RunDialog(L"Warning", COLOR_WARNING, lines, ARRAY_COUNT(lines), L"Titan BIOS", L"No"))
    {
        m_TitanBios = false;
        OfferBackupRestore();
        return;
    }

    m_TitanBios = true;
    bool main_written = false;
    if (write_partition_tables(m_TableHddBackup, NULL, &main_written))
    {
        ShowMessage(L"Done", COLOR_GREEN, main_written ? L"Written as the backup and the main table."
                                                       : L"The backup partition table was written.");
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The backup partition table could not be written.");
    }
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

void App::WarnStellarDetected()
{
    const LPCWSTR lines[] = {
        L"You are currently running Stellar.",
        L"This tool has not been tested with it.",
        L"",
        L"Returning you to your launcher.",
    };
    ShowError(lines, ARRAY_COUNT(lines));
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
    if (IsMainTableUsable() && IsBackupTableUsable() && !same_table_but_version(m_TableHdd, m_TableHddBackup))
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

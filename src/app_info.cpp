// The System information and About screens.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// "931.51 GB (1953525168 sectors)"
static void format_disk_size(ULONGLONG sectors, wchar_t *out)
{
    wchar_t size[32];
    format_human_size(sectors, size);
    swprintf(out, L"%s (%I64u sectors)", size, sectors);
}

// True if System information shows something worth a look that no other
// startup warning covers: no drive info, a BIOS size below the drive's, no
// usable table at all, or the backup missing or at its old place
bool App::SystemInfoNeedsAttention()
{
    if (!m_Drive.identify_ok)
    {
        return true;
    }

    // Without a patch it's always the 28 bit size (DisplayNoInfo covers that)
    if (m_pKernelTable != NULL && m_TotalHddSectors < m_Drive.native_sectors)
    {
        return true;
    }

    if (!IsMainTableUsable() && !IsBackupTableUsable())
    {
        return true;
    }

    return m_BackupStatus == BACKUP_MISSING || m_BackupStatus == BACKUP_FOUND_LEGACY;
}

// Shown at startup when SystemInfoNeedsAttention(), and from the menu
void App::DisplaySystemInfo()
{
    InfoRow rows[14]; // At most 12 are added
    int row_count = 0;

#define ADD_INFO_ROW(row_label, row_color) \
    rows[row_count].label = (row_label); \
    rows[row_count].color = (row_color); \
    row_count++

    // As the drive reports itself
    if (m_Drive.identify_ok)
    {
        swprintf(rows[row_count].value, L"%s", m_Drive.model[0] ? m_Drive.model : L"(empty)");
        ADD_INFO_ROW(L"HDD model", COLOR_WHITE);

        swprintf(rows[row_count].value, L"%s", m_Drive.firmware[0] ? m_Drive.firmware : L"(empty)");
        ADD_INFO_ROW(L"Firmware", COLOR_WHITE);

        format_disk_size(m_Drive.native_sectors, rows[row_count].value);
        ADD_INFO_ROW(L"Drive size", COLOR_WHITE);

        if (m_Drive.physical_sector_size > HDD_SECTOR_SIZE)
        {
            swprintf(rows[row_count].value, L"%u logical, %u physical (512e)", HDD_SECTOR_SIZE, m_Drive.physical_sector_size);
        }
        else
        {
            swprintf(rows[row_count].value, L"%u bytes", HDD_SECTOR_SIZE);
        }
        ADD_INFO_ROW(L"Sector size", COLOR_WHITE);
    }
    else
    {
        swprintf(rows[row_count].value, L"no drive info (ATA IDENTIFY failed)");
        ADD_INFO_ROW(L"HDD", COLOR_YELLOW);
    }

    // As the BIOS sees it
    const bool size_capped = m_Drive.identify_ok && m_TotalHddSectors < m_Drive.native_sectors;
    format_disk_size(m_TotalHddSectors, rows[row_count].value);
    ADD_INFO_ROW(L"BIOS size", size_capped ? COLOR_YELLOW : COLOR_WHITE);
    if (size_capped)
    {
        swprintf(rows[row_count].value, L"(less than the drive: BIOS limit)");
        ADD_INFO_ROW(L"", COLOR_YELLOW);
    }

    if (m_pKernelTable == NULL)
    {
        swprintf(rows[row_count].value, L"none (stock kernel, max 128GB)");
        ADD_INFO_ROW(L"LBA48 patch", COLOR_RED);
    }
    else if (m_FullLba48)
    {
        swprintf(rows[row_count].value, L"v%u (48 bit LBAs)", m_Lba48Version);
        ADD_INFO_ROW(L"LBA48 patch", COLOR_GREEN);
    }
    else if (m_ForceLba48V3 && m_Lba48Version > 3)
    {
        swprintf(rows[row_count].value, L"v%u, used as v3 (force_lba48_v3)", m_Lba48Version);
        ADD_INFO_ROW(L"LBA48 patch", COLOR_YELLOW);
        swprintf(rows[row_count].value, L"(32 bit LBAs, partitions max 1TB)");
        ADD_INFO_ROW(L"", COLOR_YELLOW);
    }
    else
    {
        swprintf(rows[row_count].value, L"v%u (32 bit LBAs, partitions max 1TB)", m_Lba48Version);
        ADD_INFO_ROW(L"LBA48 patch", COLOR_YELLOW);
    }

    format_disk_size(m_pTable->GetUsableLBAs(), rows[row_count].value);
    ADD_INFO_ROW(L"Usable", COLOR_WHITE);

    const bool main_valid = IsMainTableUsable();
    if (main_valid && XboxPartitionTableManipulator::HasUnknownVersion(m_TableHdd))
    {
        swprintf(rows[row_count].value, L"valid, unknown version 0x%02X (sector 0)", m_TableHdd.Version);
        ADD_INFO_ROW(L"Partition table", COLOR_YELLOW);
    }
    else if (main_valid)
    {
        swprintf(rows[row_count].value, L"valid, %s (sector 0)",
                 (m_TableHdd.Version == XBOX_TABLE_VERSION_TITAN)    ? L"Titan"
                 : (m_TableHdd.Version == XBOX_TABLE_VERSION_LBA48_V4) ? L"v4"
                                                                       : L"v3");
        ADD_INFO_ROW(L"Partition table", COLOR_GREEN);
    }
    else if (m_TitanBios && !XboxPartitionTableManipulator::HasMagic(m_TableHdd))
    {
        swprintf(rows[row_count].value, L"none, Titan BIOS uses the backup");
        ADD_INFO_ROW(L"Partition table", COLOR_GREY);
    }
    else
    {
        swprintf(rows[row_count].value, L"%s", XboxPartitionTableManipulator::HasMagic(m_TableHdd) ? L"damaged (sector 0)" : L"missing");
        ADD_INFO_ROW(L"Partition table", COLOR_RED);
    }

    wchar_t *value = rows[row_count].value;
    Color backup_color = COLOR_YELLOW;
    switch (m_BackupStatus)
    {
    case BACKUP_FOUND:
        if (!IsBackupTableUsable())
        {
            swprintf(value, L"damaged (sector %I64u)", m_pTable->GetBackupTableLba());
            backup_color = COLOR_RED;
        }
        else if (!main_valid || same_table_but_version(m_TableHdd, m_TableHddBackup))
        {
            swprintf(value, L"valid (sector %I64u)", m_pTable->GetBackupTableLba());
            backup_color = (main_valid || m_TitanBios) ? COLOR_GREEN : COLOR_YELLOW;
        }
        else
        {
            swprintf(value, L"differs from main! (sector %I64u)", m_pTable->GetBackupTableLba());
        }
        break;
    case BACKUP_FOUND_LEGACY:
        if (IsBackupTableUsable())
        {
            swprintf(value, L"valid, old location (sector %I64u)", m_pTable->GetLastSectorLba());
        }
        else
        {
            swprintf(value, L"damaged, old location (sector %I64u)", m_pTable->GetLastSectorLba());
            backup_color = COLOR_RED;
        }
        break;
    case BACKUP_MISSING:
        swprintf(value, L"missing (sector %I64u)", m_pTable->GetBackupTableLba());
        break;
    case BACKUP_OVERLAPPED:
        swprintf(value, L"none, last partition covers it");
        break;
    case BACKUP_OVERLAPPED_MATCH:
        swprintf(value, L"matches, but last partition covers it");
        break;
    default:
        if (m_pKernelTable == NULL)
        {
            swprintf(value, L"not used (no LBA48 patch)");
        }
        else
        {
            swprintf(value, L"none (no room after stock partitions)");
        }
        backup_color = COLOR_GREY;
        break;
    }
    ADD_INFO_ROW(L"Backup table", backup_color);

    swprintf(rows[row_count].value, L"%s", m_Ui.GetVideoModeName());
    ADD_INFO_ROW(L"Video mode", COLOR_WHITE);

#undef ADD_INFO_ROW

    ShowInfoPanel(L"System information", rows, row_count);
}

void App::DisplayAbout()
{
    InfoRow rows[7];
    int row_count = 0;

    // Labels are grey, values all white
#define ADD_ABOUT_ROW(row_label, row_value) \
    rows[row_count].label = (row_label); \
    swprintf(rows[row_count].value, L"%s", (row_value)); \
    rows[row_count].color = COLOR_WHITE; \
    row_count++

    ADD_ABOUT_ROW(L"Version", L"XBpartitioner " XBPARTITIONER_VERSION);
    ADD_ABOUT_ROW(L"Updated by", L"ZeeAlucard");
    ADD_ABOUT_ROW(L"Based on", L"the original XBpartitioner 1.3");
    ADD_ABOUT_ROW(L"Format fix", L"PrometheOS");
    ADD_ABOUT_ROW(L"Kernel defs", L"everlame, Team Evox, Woodoo");
    ADD_ABOUT_ROW(L"LBA48", L"oz_paulb/Cerbios/gaasedelen");
    ADD_ABOUT_ROW(L"Built with", L"RXDK by Team Resurgent");

#undef ADD_ABOUT_ROW

    ShowInfoPanel(L"About", rows, row_count);
}

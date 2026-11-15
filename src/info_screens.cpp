#include "stdafx.h"
#include "info_screens.h"
#include "array_length.h"
#include "main_screen.h" // PROGRAM_NAME_AND_VERSION
#include "partition_format.h"
#include <stdio.h>
#include <string.h>

// "931.51 GB (1953525168 sectors)", into an info row's value
static void format_disk_size(ULONGLONG sectors, wchar_t *out)
{
    wchar_t size[32];
    FormatHumanSize(sectors, size);
    FormatWide(out, INFO_ROW_VALUE_COUNT, L"%s (%I64u sectors)", size, sectors);
}

wchar_t *InfoScreens::InfoRowList::Add(LPCWSTR label, Color color)
{
    InfoRow &row = m_rows[m_count++];
    row.label = label;
    row.color = color;
    row.value[0] = L'\0';
    return row.value;
}

InfoScreens::InfoScreens(DialogPresenter &dialogs, const InfoSources &sources)
    : m_dialogs(dialogs), m_tables(sources.tables), m_session(sources.session), m_settings(sources.settings), m_drive(sources.drive),
      m_ui(sources.ui)
{
}

bool InfoScreens::SystemInfoNeedsAttention() const
{
    if (!m_drive.identify_ok)
    {
        return true;
    }

    // Without a patch it's always the 28 bit size (the startup checks cover that)
    if (m_tables.HasLba48Patch() && m_tables.TotalSectors() < m_drive.native_sectors)
    {
        return true;
    }

    if (!m_tables.IsMainTableUsable() && !m_tables.IsBackupTableUsable())
    {
        return true;
    }

    return m_tables.Backup() == BACKUP_MISSING || m_tables.Backup() == BACKUP_FOUND_LEGACY;
}

// Shown at startup when SystemInfoNeedsAttention(), and from the menu
void InfoScreens::ShowSystemInfo()
{
    InfoRowList rows;
    AddDriveRows(rows);
    AddBiosSizeRows(rows);
    AddLba48Rows(rows);
    format_disk_size(m_session.Edited().GetUsableLBAs(), rows.Add(L"Usable", COLOR_WHITE));
    AddMainTableRow(rows);
    AddBackupTableRow(rows);
    FormatWide(rows.Add(L"Video mode", COLOR_WHITE), INFO_ROW_VALUE_COUNT, L"%s", m_ui.GetVideoModeName());

    m_dialogs.ShowInfoPanel(L"System information", rows.Rows(), rows.Count());
}

void InfoScreens::AddDriveRows(InfoRowList &rows) const
{
    if (!m_drive.identify_ok)
    {
        FormatWide(rows.Add(L"HDD", COLOR_YELLOW), INFO_ROW_VALUE_COUNT, L"no drive info (ATA IDENTIFY failed)");
        return;
    }

    FormatWide(rows.Add(L"HDD model", COLOR_WHITE), INFO_ROW_VALUE_COUNT, L"%s", m_drive.model[0] ? m_drive.model : L"(empty)");
    FormatWide(rows.Add(L"Firmware", COLOR_WHITE), INFO_ROW_VALUE_COUNT, L"%s", m_drive.firmware[0] ? m_drive.firmware : L"(empty)");
    format_disk_size(m_drive.native_sectors, rows.Add(L"Drive size", COLOR_WHITE));

    wchar_t *sector_size = rows.Add(L"Sector size", COLOR_WHITE);
    if (m_drive.physical_sector_size > HDD_SECTOR_SIZE)
    {
        FormatWide(sector_size, INFO_ROW_VALUE_COUNT, L"%u logical, %u physical (512e)", HDD_SECTOR_SIZE, m_drive.physical_sector_size);
    }
    else
    {
        FormatWide(sector_size, INFO_ROW_VALUE_COUNT, L"%u bytes", HDD_SECTOR_SIZE);
    }
    AddDriveTypeRow(rows);
}

// The Xbox can't send TRIM; fatxtool can
void InfoScreens::AddDriveTypeRow(InfoRowList &rows) const
{
    if (m_drive.trim)
    {
        FormatWide(rows.Add(L"Drive type", COLOR_GREEN), INFO_ROW_VALUE_COUNT, L"%s, TRIM: fatxtool trim on a PC", m_drive.rotation_rate == 1 ? L"SSD" : L"supports TRIM");
        return;
    }

    wchar_t *type = rows.Add(L"Drive type", COLOR_WHITE);
    if (m_drive.rotation_rate == 1)
    {
        FormatWide(type, INFO_ROW_VALUE_COUNT, L"SSD, no TRIM");
    }
    else if (m_drive.rotation_rate >= 0x0401 && m_drive.rotation_rate < 0xFFFF)
    {
        FormatWide(type, INFO_ROW_VALUE_COUNT, L"hard drive (%u rpm)", m_drive.rotation_rate);
    }
    else
    {
        FormatWide(type, INFO_ROW_VALUE_COUNT, L"not reported");
    }
}

void InfoScreens::AddBiosSizeRows(InfoRowList &rows) const
{
    const bool size_capped = m_drive.identify_ok && m_tables.TotalSectors() < m_drive.native_sectors;
    format_disk_size(m_tables.TotalSectors(), rows.Add(L"BIOS size", size_capped ? COLOR_YELLOW : COLOR_WHITE));
    if (size_capped)
    {
        FormatWide(rows.Add(L"", COLOR_YELLOW), INFO_ROW_VALUE_COUNT, L"(less than the drive: BIOS limit)");
    }
}

void InfoScreens::AddLba48Rows(InfoRowList &rows) const
{
    const unsigned int version = m_tables.Lba48Version();
    if (!m_tables.HasLba48Patch())
    {
        FormatWide(rows.Add(L"LBA48 patch", COLOR_RED), INFO_ROW_VALUE_COUNT, L"none (stock kernel, max 128GB)");
    }
    else if (m_tables.FullLba48())
    {
        FormatWide(rows.Add(L"LBA48 patch", COLOR_GREEN), INFO_ROW_VALUE_COUNT, L"v%u (48 bit LBAs)", version);
    }
    else if (m_settings.ForceLba48V3() && version > 3)
    {
        FormatWide(rows.Add(L"LBA48 patch", COLOR_YELLOW), INFO_ROW_VALUE_COUNT, L"v%u, used as v3 (force_lba48_v3)", version);
        FormatWide(rows.Add(L"", COLOR_YELLOW), INFO_ROW_VALUE_COUNT, L"(32 bit LBAs, partitions max 1TB)");
    }
    else
    {
        FormatWide(rows.Add(L"LBA48 patch", COLOR_YELLOW), INFO_ROW_VALUE_COUNT, L"v%u (32 bit LBAs, partitions max 1TB)", version);
    }
}

void InfoScreens::AddMainTableRow(InfoRowList &rows) const
{
    const XboxPartitionTable &main = m_tables.MainTable();
    if (m_tables.IsMainTableUsable() && PartitionTableEditor::HasUnknownVersion(main))
    {
        FormatWide(rows.Add(L"Partition table", COLOR_YELLOW), INFO_ROW_VALUE_COUNT, L"valid, unknown version 0x%02X (sector 0)", main.Version);
    }
    else if (m_tables.IsMainTableUsable())
    {
        const LPCWSTR version = (main.Version == XBOX_TABLE_VERSION_TITAN) ? L"Titan" : (main.Version == XBOX_TABLE_VERSION_LBA48_V4) ? L"v4" : L"v3";
        FormatWide(rows.Add(L"Partition table", COLOR_GREEN), INFO_ROW_VALUE_COUNT, L"valid, %s (sector 0)", version);
    }
    else if (m_tables.TitanBios() && !PartitionTableEditor::HasMagic(main))
    {
        FormatWide(rows.Add(L"Partition table", COLOR_GREY), INFO_ROW_VALUE_COUNT, L"none, Titan BIOS uses the backup");
    }
    else
    {
        FormatWide(rows.Add(L"Partition table", COLOR_RED), INFO_ROW_VALUE_COUNT, L"%s", PartitionTableEditor::HasMagic(main) ? L"damaged (sector 0)" : L"missing");
    }
}

// The text depends on the status, the colour also on whether it can be trusted
void InfoScreens::AddBackupTableRow(InfoRowList &rows) const
{
    const bool main_valid = m_tables.IsMainTableUsable();
    const ULONGLONG backup_lba = m_session.Edited().GetBackupTableLba();
    const ULONGLONG last_sector_lba = m_session.Edited().GetLastSectorLba();
    wchar_t value[80];
    Color color = COLOR_YELLOW;

    switch (m_tables.Backup())
    {
    case BACKUP_FOUND:
        if (!m_tables.IsBackupTableUsable())
        {
            FormatWide(value, ArrayLength(value), L"damaged (sector %I64u)", backup_lba);
            color = COLOR_RED;
        }
        else if (!main_valid || SameTableButVersion(m_tables.MainTable(), m_tables.BackupTable()))
        {
            FormatWide(value, ArrayLength(value), L"valid (sector %I64u)", backup_lba);
            color = (main_valid || m_tables.TitanBios()) ? COLOR_GREEN : COLOR_YELLOW;
        }
        else
        {
            FormatWide(value, ArrayLength(value), L"differs from main! (sector %I64u)", backup_lba);
        }
        break;
    case BACKUP_FOUND_LEGACY:
        if (m_tables.IsBackupTableUsable())
        {
            FormatWide(value, ArrayLength(value), L"valid, old location (sector %I64u)", last_sector_lba);
        }
        else
        {
            FormatWide(value, ArrayLength(value), L"damaged, old location (sector %I64u)", last_sector_lba);
            color = COLOR_RED;
        }
        break;
    case BACKUP_MISSING:
        FormatWide(value, ArrayLength(value), L"missing (sector %I64u)", backup_lba);
        break;
    case BACKUP_OVERLAPPED:
        FormatWide(value, ArrayLength(value), L"none, last partition covers it");
        break;
    case BACKUP_OVERLAPPED_MATCH:
        FormatWide(value, ArrayLength(value), L"matches, but last partition covers it");
        break;
    default:
        FormatWide(value, ArrayLength(value), m_tables.HasLba48Patch() ? L"none (no room after stock partitions)" : L"not used (no LBA48 patch)");
        color = COLOR_GREY;
        break;
    }
    FormatWide(rows.Add(L"Backup table", color), INFO_ROW_VALUE_COUNT, L"%s", value);
}

// Labels are grey, values all white
void InfoScreens::ShowAbout()
{
    static const LPCWSTR about[][2] = {
        {L"Version", PROGRAM_NAME_AND_VERSION},
        {L"Updated by", L"ZeeAlucard"},
        {L"Based on", L"the original XBpartitioner 1.3"},
        {L"Format fix", L"PrometheOS"},
        {L"Kernel defs", L"everlame, Team Evox, Woodoo"},
        {L"LBA48", L"oz_paulb/Cerbios/doom"},
        {L"Built with", L"RXDK by Team Resurgent"},
    };

    InfoRowList rows;
    for (size_t i = 0; i < ArrayLength(about); i++)
    {
        FormatWide(rows.Add(about[i][0], COLOR_WHITE), INFO_ROW_VALUE_COUNT, L"%s", about[i][1]);
    }
    m_dialogs.ShowInfoPanel(L"About", rows.Rows(), rows.Count());
}

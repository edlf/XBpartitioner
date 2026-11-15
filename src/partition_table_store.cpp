#include "stdafx.h"
#include "partition_table_store.h"
#include "partition_format.h"
#include <string.h>

PartitionTableStore::PartitionTableStore(HddAccess &hdd)
    : m_hdd(hdd), m_total_sectors(0), m_physical_4k(false), m_full_lba48(false), m_lba48_version(0), m_kernel_memory(NULL),
      m_backup_status(BACKUP_NOT_USED), m_titan_bios(false)
{
    memset(&m_kernel, 0, sizeof(m_kernel));
    memset(&m_main, 0, sizeof(m_main));
    memset(&m_backup, 0, sizeof(m_backup));
}

void PartitionTableStore::ReadDiskSize(bool physical_4k)
{
    m_total_sectors = m_hdd.ReadSizeInSectors();
    m_physical_4k = physical_4k;
}

bool PartitionTableStore::ReadKernelTable(const Settings &settings)
{
    memset(&m_kernel, 0, sizeof(m_kernel));
    const Lba48Patch patch = m_hdd.ReadLba48Patch();
    if (!patch.present)
    {
        return false;
    }

    m_lba48_version = patch.version;
    // force_lba48_v3: as if the BIOS were v3
    m_full_lba48 = (m_lba48_version > 3) && !settings.ForceLba48V3();
    m_kernel_memory = patch.kernel_table;
    memcpy(&m_kernel, m_kernel_memory, sizeof(XboxPartitionTable));
    return true;
}

void PartitionTableStore::ReadDiskTables()
{
    m_main = ReadMainTable();
    // After the main one: an overlapped backup must match it
    m_backup = ReadBackupTable();
}

bool PartitionTableStore::IsMainTableUsable() const
{
    return PartitionTableEditor::IsTableUsable(m_main, UsableCheckSize());
}

bool PartitionTableStore::IsBackupTableUsable() const
{
    return PartitionTableEditor::IsTableUsable(m_backup, UsableCheckSize());
}

PartitionTableEditor PartitionTableStore::Manipulate(const XboxPartitionTable &table) const
{
    return PartitionTableEditor(table, m_total_sectors, m_full_lba48, m_physical_4k);
}

PartitionTableEditor PartitionTableStore::StockLayout() const
{
    return PartitionTableEditor(m_total_sectors, m_full_lba48, m_physical_4k);
}

TableWritePlan PartitionTableStore::Prepare(const XboxPartitionTable &table) const
{
    TableWritePlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.write_main = true;

    // Sector 0 says how its LBAs are stored: v4, or v3 (also with force_lba48_v3)
    plan.table = table;
    plan.table.Version = PartitionTableEditor::VersionFor(m_full_lba48);

    // Without a known disk size the layout can't be trusted
    if (m_total_sectors == 0)
    {
        return plan;
    }

    const PartitionTableEditor to_write = Manipulate(plan.table);
    int reserved = 0;
    int first = 0;
    int second = 0;
    if (to_write.FindPartitionBeforeUsableStart(&reserved) || to_write.FindConflict(&first, &second))
    {
        return plan;
    }

    // No backup without an lba48 patch (the size is the 28 bit one), without
    // room for it, or when the last partition covers its sector
    plan.backup_possible = HasLba48Patch() && (to_write.GetBackupTableLba() != 0) && !to_write.OverlapsBackupTable();
    plan.writable = true;
    return plan;
}

TableWriteOutcome PartitionTableStore::Write(const XboxPartitionTable &table, bool write_main)
{
    const TableWritePlan plan = Prepare(table);
    const bool write_backup = plan.backup_possible;
    if (!plan.writable || (!write_backup && !write_main))
    {
        return TableWriteOutcome::NotWritten();
    }

    XboxPartitionTable backup = plan.table;
    if (m_titan_bios)
    {
        backup.Version = XBOX_TABLE_VERSION_TITAN;
    }

    const TableSectorWrite sectors = {write_backup ? &backup : NULL, Manipulate(plan.table).GetBackupTableLba(), write_main ? &plan.table : NULL};
    TableWriteOutcome outcome = TableWriteOutcome::NotWritten();
    outcome.written = m_hdd.WriteTableSectors(sectors);
    outcome.main_written = outcome.written && write_main;
    outcome.backup_written = outcome.written && write_backup;

    // The kernel gets the copy it reads; KernelTable() catches up when re-read
    if (outcome.written && HasLba48Patch())
    {
        memcpy(m_kernel_memory, m_titan_bios ? &backup : &plan.table, sizeof(XboxPartitionTable));
    }
    return outcome;
}

XboxPartitionTable PartitionTableStore::ReadMainTable() const
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    if (m_total_sectors > 0)
    {
        table = m_hdd.ReadTableAt(0);
    }
    return table;
}

// Where the backup is depends on the disk; which sectors hold partition data,
// on the table in use (the kernel's)
XboxPartitionTable PartitionTableStore::ReadBackupTable()
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    // Without a patch the "last sector" may be mid disk (28 bit size)
    const PartitionTableEditor in_use = Manipulate(m_kernel);
    if (in_use.GetBackupTableLba() == 0 || !HasLba48Patch())
    {
        m_backup_status = BACKUP_NOT_USED;
        return table;
    }

    const bool backup_overlapped = in_use.OverlapsBackupTable();
    table = m_hdd.ReadTableAt(in_use.GetBackupTableLba());
    if (!backup_overlapped && PartitionTableEditor::HasMagic(table))
    {
        m_backup_status = BACKUP_FOUND;
        return table;
    }

    // This sector belongs to a partition, but other tools put a backup here.
    // If it matches the table in use, the block holds no data (see
    // App::OfferShrinkForBackup()).
    if (backup_overlapped && PartitionTableEditor::IsTableUsable(table, m_total_sectors) &&
        PartitionTableEditor::IsTableUsable(m_main, m_total_sectors) && SameTableButVersion(table, m_main) &&
        SameTableButVersion(table, m_kernel))
    {
        m_backup_status = BACKUP_OVERLAPPED_MATCH;
        return table;
    }
    memset(&table, 0, sizeof(table));

    // Older builds put the backup in the last sector, also on 512e drives:
    // read only fallback, the next write uses the new place
    m_backup_status = backup_overlapped ? BACKUP_OVERLAPPED : BACKUP_MISSING;
    const uint64_t last_sector_lba = in_use.GetLastSectorLba();
    if (last_sector_lba != in_use.GetBackupTableLba() && !in_use.OverlapsSector(last_sector_lba))
    {
        table = m_hdd.ReadTableAt(last_sector_lba);
        if (PartitionTableEditor::HasMagic(table))
        {
            m_backup_status = BACKUP_FOUND_LEGACY;
        }
    }
    return table;
}

ULONGLONG PartitionTableStore::UsableCheckSize() const
{
    return HasLba48Patch() ? m_total_sectors : 0;
}

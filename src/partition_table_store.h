#ifndef PARTITION_TABLE_STORE_H_
#define PARTITION_TABLE_STORE_H_

#include "hdd_access.h"
#include "partition.hpp"
#include "settings.h"

enum BackupStatus
{
    BACKUP_NOT_USED = 0,    // No backup location: no patch, unknown size, or stock 8GB drive
    BACKUP_MISSING,         // Location known but no valid table there
    BACKUP_FOUND,
    BACKUP_FOUND_LEGACY,    // At the old location (last sector) on a 512e drive
    BACKUP_OVERLAPPED,      // The last partition covers the backup sector
    BACKUP_OVERLAPPED_MATCH // Covered, but holds a valid copy of the table
};

// A table checked for writing, and where it goes (see App::PlanTableWrite)
struct TableWritePlan
{
    bool writable; // False: it must not be written, or nothing the BIOS reads could be
    XboxPartitionTable table;
    bool backup_possible;
    bool write_main; // Sector 0 too; only a Titan BIOS may leave it out
};

// What writing the partition tables did
struct TableWriteOutcome
{
    bool written;
    bool main_written; // False while written: the backup only (Titan BIOS)
    bool backup_written;
    bool error_shown; // The failure was already explained to the user

    static TableWriteOutcome NotWritten()
    {
        TableWriteOutcome outcome = {false, false, false, false};
        return outcome;
    }

    static TableWriteOutcome ErrorShown()
    {
        TableWriteOutcome outcome = {false, false, false, true};
        return outcome;
    }
};

// The partition tables as they are: the disk's size and table mode, the
// kernel's table (LBA48 patch) and the ones on disk, and writing them. No
// dialogs and no editor, so a worker thread can write through it.
class PartitionTableStore
{
  public:
    explicit PartitionTableStore(HddAccess &hdd);

    // At startup, before any table: physical_4k decides where the backup goes
    void ReadDiskSize(bool physical_4k);
    // The kernel's table, zeroed without the LBA48 patch. The patch version
    // and the table mode only change when the patch is found.
    bool ReadKernelTable(const Settings &settings);
    // The main and backup tables on disk, after the kernel's
    void ReadDiskTables();

    ULONGLONG TotalSectors() const
    {
        return m_total_sectors;
    }
    bool Physical4k() const
    {
        return m_physical_4k;
    }
    // LBA48 v4 tables (48 bit LBAs); false on v3, also with force_lba48_v3
    bool FullLba48() const
    {
        return m_full_lba48;
    }
    DWORD Lba48Version() const
    {
        return m_lba48_version;
    }
    bool HasLba48Patch() const
    {
        return m_kernel_memory != NULL;
    }
    const XboxPartitionTable &KernelTable() const
    {
        return m_kernel;
    }
    const XboxPartitionTable &MainTable() const
    {
        return m_main;
    }
    const XboxPartitionTable &BackupTable() const
    {
        return m_backup;
    }
    BackupStatus Backup() const
    {
        return m_backup_status;
    }
    bool IsMainTableUsable() const;
    bool IsBackupTableUsable() const;

    // Titan patched BIOS (it only uses the backup table): the user said so,
    // or the backup has Titan's version
    bool TitanBios() const
    {
        return m_titan_bios;
    }
    void SetTitanBios(bool titan_bios)
    {
        m_titan_bios = titan_bios;
    }

    // An editor for a table on this disk, in this mode
    PartitionTableEditor Manipulate(const XboxPartitionTable &table) const;
    // The stock layout on this disk
    PartitionTableEditor StockLayout() const;

    // Not writable if it must not be written; callers explain why beforehand.
    // Sector 0 is always a target here.
    TableWritePlan Prepare(const XboxPartitionTable &table) const;
    // To the disk and to the kernel. Re-read the tables afterwards.
    TableWriteOutcome Write(const XboxPartitionTable &table, bool write_main);

  private:
    XboxPartitionTable ReadMainTable() const;
    XboxPartitionTable ReadBackupTable();
    // Sizes are only checked against the disk with the patch (else 28 bit)
    ULONGLONG UsableCheckSize() const;

    HddAccess &m_hdd;
    ULONGLONG m_total_sectors;
    bool m_physical_4k;
    bool m_full_lba48;
    DWORD m_lba48_version;
    XboxPartitionTable *m_kernel_memory; // The kernel's own copy, NULL without the patch
    XboxPartitionTable m_kernel;
    XboxPartitionTable m_main;
    XboxPartitionTable m_backup;
    BackupStatus m_backup_status;
    bool m_titan_bios;
};

#endif // PARTITION_TABLE_STORE_H_

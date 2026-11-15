#ifndef HDD_ACCESS_H_
#define HDD_ACCESS_H_

#include <xtl.h>
#include "Undocumented.h"
#include "partition.hpp"

// From ATA IDENTIFY DEVICE
struct DriveInfo
{
    bool identify_ok; // false: IDENTIFY failed, the rest is empty
    wchar_t model[41];
    wchar_t firmware[9];
    ULONGLONG native_sectors; // Independent of the BIOS
    unsigned int physical_sector_size;
    bool physical_4k;           // 512e drive
    unsigned int rotation_rate; // 1: SSD, 0: not reported, else rpm
    bool trim;                  // Drive supports TRIM (the Xbox can't send it)
};

// What an LBA48 patched kernel reports about itself
struct Lba48Patch
{
    bool present;
    DWORD version;
    // The table the kernel uses, in its memory: writing it changes the
    // partitions at once
    XboxPartitionTable *kernel_table;
};

// The partition table sectors one write updates; NULL leaves one alone
struct TableSectorWrite
{
    const XboxPartitionTable *backup;
    ULONGLONG backup_lba;
    const XboxPartitionTable *main; // Sector 0
};

// Which of the requested sectors were written
struct TableSectorsWritten
{
    bool backup;
    bool main;
};

// A synchronous read or write that succeeded and moved every byte asked for
inline bool TransferredAll(NTSTATUS status, const IO_STATUS_BLOCK &status_block, ULONG length)
{
    return NT_SUCCESS(status) && status_block.Information == length;
}

// The hard drive through the kernel: what it is, the LBA48 patch, and
// reading and writing sectors. Holds no state; every call opens the disk again.
class HddAccess
{
  public:
    // From the kernel's geometry, which the BIOS decides. 0 if unknown.
    ULONGLONG ReadSizeInSectors();
    // On failure identify_ok is false and the drive counts as plain 512 byte
    DriveInfo ReadIdentify();
    // Not present without a patched kernel (or an unknown one)
    Lba48Patch ReadLba48Patch();

    // Cleared first; false if it couldn't be read
    bool ReadSector(ULONGLONG lba, BYTE *sector);
    // A failed read gives a table without magic
    XboxPartitionTable ReadTableAt(ULONGLONG lba);
    // From the FATX header in a partition's first sectors. 0 if not
    // formatted, unreadable or an odd size.
    unsigned int ReadClusterSizeKb(const ANSI_STRING &partition_path);
    // The backup first: if it fails, sector 0 isn't written
    TableSectorsWritten WriteTableSectors(const TableSectorWrite &write);

  private:
    // The whole disk (Partition0), shared, synchronous
    NTSTATUS OpenDisk(HANDLE *handle, ACCESS_MASK access);
};

#endif // HDD_ACCESS_H_

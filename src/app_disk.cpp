// Disk and kernel access: drive info, the LBA48 patch, the tables on disk,
// FATX cluster sizes.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

#include <winbase.h>
#include "Winioctl.h"

// Passed to the kernel: must match its layout (32 bit pointers)
static_assert(sizeof(ANSI_STRING) == 8, "ANSI_STRING layout is wrong!");
static_assert(sizeof(OBJECT_ATTRIBUTES) == 12, "OBJECT_ATTRIBUTES layout is wrong!");
static_assert(sizeof(IO_STATUS_BLOCK) == 8, "IO_STATUS_BLOCK layout is wrong!");
static_assert(sizeof(DISK_GEOMETRY) == 24, "DISK_GEOMETRY layout is wrong!");

ULONGLONG App::get_hdd_size()
{
    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\partition0");

    HANDLE handle;
    OBJECT_ATTRIBUTES obj_attr;
    obj_attr.RootDirectory = 0;
    obj_attr.ObjectName = &a_file;
    obj_attr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io_stat_block;
    unsigned int stat = NtOpenFile(&handle, (GENERIC_READ | 0x00100000), &obj_attr, &io_stat_block, (FILE_SHARE_READ | FILE_SHARE_WRITE), 0x10);

    if (stat != STATUS_SUCCESS)
    {
        // Unknown size (not the NTSTATUS, which would read as a sector count)
        return 0;
    }

    unsigned int geom_ioctl_cmd_in_buf[100];
    memset(geom_ioctl_cmd_in_buf, 0, sizeof(geom_ioctl_cmd_in_buf));
    geom_ioctl_cmd_in_buf[0] = IOCTL_SUBCMD_GET_INFO;

    DISK_GEOMETRY DiskGeometry;
    stat = NtDeviceIoControlFile(handle,
                                 0,
                                 0,
                                 0,
                                 &io_stat_block,
                                 IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                 geom_ioctl_cmd_in_buf,
                                 sizeof(geom_ioctl_cmd_in_buf),
                                 &DiskGeometry,
                                 sizeof(DiskGeometry));
    NtClose(handle);

    ULONGLONG total_sectors = 0ULL;
    if (stat == STATUS_SUCCESS)
    {
        total_sectors = DiskGeometry.Cylinders.QuadPart;
    }

    return total_sectors;
}

// IOCTL_IDE_PASS_THROUGH input/output, as used by the Xbox IDE driver
typedef struct _XB_IDE_REGS
{
    UCHAR bFeaturesReg;
    UCHAR bSectorCountReg;
    UCHAR bSectorNumberReg;
    UCHAR bCylLowReg;
    UCHAR bCylHighReg;
    UCHAR bDriveHeadReg;
    UCHAR bCommandReg;
    UCHAR bHostSendsData;
} XB_IDE_REGS;

typedef struct _XB_ATA_PASS_THROUGH
{
    XB_IDE_REGS IdeReg;
    ULONG DataBufferSize;
    PVOID DataBuffer;
} XB_ATA_PASS_THROUGH;

static_assert(sizeof(XB_IDE_REGS) == 8, "XB_IDE_REGS layout is wrong!");
static_assert(offsetof(XB_ATA_PASS_THROUGH, DataBufferSize) == 8, "XB_ATA_PASS_THROUGH layout is wrong!");
static_assert(offsetof(XB_ATA_PASS_THROUGH, DataBuffer) == 12, "XB_ATA_PASS_THROUGH layout is wrong!");
static_assert(sizeof(XB_ATA_PASS_THROUGH) == 16, "XB_ATA_PASS_THROUGH layout is wrong!");

#define ATA_COMMAND_IDENTIFY_DEVICE 0xEC
#define ATA_DRIVE_HEAD_MASTER 0xA0

// ATA IDENTIFY DEVICE words used here
#define ATA_ID_FIRMWARE_WORD 23           // 4 words, 8 characters
#define ATA_ID_MODEL_WORD 27              // 20 words, 40 characters
#define ATA_ID_LBA28_SECTORS_WORD 60      // 2 words
#define ATA_ID_COMMAND_SET_2_WORD 83      // bit 10: 48 bit addressing supported
#define ATA_ID_LBA48_SECTORS_WORD 100     // 4 words
#define ATA_ID_SECTOR_SIZE_WORD 106       // physical/logical sector size
#define ATA_ID_DATA_SET_MANAGEMENT_WORD 169 // Bit 0: TRIM
#define ATA_ID_ROTATION_RATE_WORD 217     // 1: SSD, 0: not reported, else rpm

// ATA strings store two characters per word, high byte first, padded with spaces
static void ata_string(const USHORT *words, int word_count, wchar_t *out)
{
    int length = 0;
    for (int i = 0; i < word_count; i++)
    {
        out[length++] = (wchar_t)(words[i] >> 8);
        out[length++] = (wchar_t)(words[i] & 0xFF);
    }

    // Trailing spaces, and NULs some drives pad with
    while (length > 0 && (out[length - 1] == L' ' || out[length - 1] == L'\0'))
    {
        length--;
    }
    out[length] = L'\0';

    int skip = 0;
    while (out[skip] == L' ')
    {
        skip++;
    }
    if (skip > 0)
    {
        memmove(out, out + skip, (length - skip + 1) * sizeof(wchar_t));
    }
}

// ATA IDENTIFY DEVICE: model, firmware, native size, sector size. On
// failure identify_ok is false and the drive counts as plain 512 byte.
void App::read_drive_identify()
{
    memset(&m_Drive, 0, sizeof(m_Drive));

    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\Partition0");

    OBJECT_ATTRIBUTES obj_attr;
    InitializeObjectAttributes(&obj_attr, &a_file, OBJ_CASE_INSENSITIVE, NULL);

    HANDLE handle;
    IO_STATUS_BLOCK io_stat_block;
    NTSTATUS stat = NtOpenFile(
        &handle, SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE, &obj_attr, &io_stat_block, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(stat))
    {
        return;
    }

    USHORT identify[256];
    memset(identify, 0, sizeof(identify));

    XB_ATA_PASS_THROUGH ata;
    memset(&ata, 0, sizeof(ata));
    ata.IdeReg.bSectorCountReg = 1;
    ata.IdeReg.bDriveHeadReg = ATA_DRIVE_HEAD_MASTER;
    ata.IdeReg.bCommandReg = ATA_COMMAND_IDENTIFY_DEVICE;
    ata.IdeReg.bHostSendsData = FALSE;
    ata.DataBufferSize = sizeof(identify);
    ata.DataBuffer = identify;

    stat = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &io_stat_block, IOCTL_IDE_PASS_THROUGH, &ata, sizeof(ata), &ata, sizeof(ata));
    NtClose(handle);

    if (!NT_SUCCESS(stat))
    {
        return;
    }

    m_Drive.identify_ok = true;
    ata_string(&identify[ATA_ID_MODEL_WORD], 20, m_Drive.model);
    ata_string(&identify[ATA_ID_FIRMWARE_WORD], 4, m_Drive.firmware);

    // 48 bit count if supported, else the 28 bit one
    if (identify[ATA_ID_COMMAND_SET_2_WORD] & 0x0400)
    {
        m_Drive.native_sectors = (ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD] | ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 1] << 16) |
                                 ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 2] << 32) | ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 3] << 48);
    }
    else
    {
        m_Drive.native_sectors = (ULONGLONG)identify[ATA_ID_LBA28_SECTORS_WORD] | ((ULONGLONG)identify[ATA_ID_LBA28_SECTORS_WORD + 1] << 16);
    }

    // Word 106:
    //   bits 15:14 = 01  word is valid (else: old drive, 512 byte physical sectors)
    //   bit 13         multiple logical sectors per physical sector
    //   bits 3:0       log2(logical sectors per physical sector), 3 = 8 = 4K
    m_Drive.physical_sector_size = HDD_SECTOR_SIZE;
    const USHORT sector_size_info = identify[ATA_ID_SECTOR_SIZE_WORD];
    if ((sector_size_info & 0xC000) == 0x4000 && (sector_size_info & 0x2000) != 0)
    {
        m_Drive.physical_sector_size = HDD_SECTOR_SIZE << (sector_size_info & 0x000F);
    }

    m_Drive.physical_4k = m_Drive.physical_sector_size >= 4096;

    // This is the drive's answer even through an IDE adapter, which may not
    // pass TRIM on
    m_Drive.rotation_rate = identify[ATA_ID_ROTATION_RATE_WORD];
    m_Drive.trim = (identify[ATA_ID_DATA_SET_MANAGEMENT_WORD] & 0x0001) != 0;
}

bool App::read_lba48_info(XboxPartitionTable *p_table)
{
    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\partition0");

    HANDLE handle;
    OBJECT_ATTRIBUTES obj_attr;
    obj_attr.RootDirectory = 0;
    obj_attr.ObjectName = &a_file;
    obj_attr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io_stat_block;
    unsigned int stat = NtOpenFile(&handle, (GENERIC_READ | 0x00100000), &obj_attr, &io_stat_block, (FILE_SHARE_READ | FILE_SHARE_WRITE), 0x10);

    if (stat != STATUS_SUCCESS)
    {
        return false;
    }

    memset(p_table, 0, sizeof(XboxPartitionTable));

    unsigned int lba48_ioctl_cmd_in_buf[100];
    unsigned int lba48_ioctl_cmd_out_buf[100];

    memset(lba48_ioctl_cmd_in_buf, 0, sizeof(lba48_ioctl_cmd_in_buf));
    lba48_ioctl_cmd_in_buf[0] = IOCTL_SUBCMD_GET_INFO;

    memset(lba48_ioctl_cmd_out_buf, 0, sizeof(lba48_ioctl_cmd_out_buf));

    stat = NtDeviceIoControlFile(handle,
                                 0,
                                 0,
                                 0,
                                 &io_stat_block,
                                 IOCTL_CMD_LBA48_ACCESS,
                                 lba48_ioctl_cmd_in_buf,
                                 sizeof(lba48_ioctl_cmd_in_buf),
                                 lba48_ioctl_cmd_out_buf,
                                 sizeof(lba48_ioctl_cmd_out_buf));

    NtClose(handle);
    if (stat != STATUS_SUCCESS)
    {
        return false;
    }

    if ((lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_MAGIC1_IDX] != LBA48_GET_INFO_MAGIC1_VAL) ||
        (lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_MAGIC2_IDX] != LBA48_GET_INFO_MAGIC2_VAL))
    {
        return false;
    }

    // Compatible with Paul's lba48 patched kernels
    unsigned int partition_table_addr = lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_LOWCODE_BASE_IDX];
    partition_table_addr += lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_PART_TABLE_OFS_IDX];

    m_Lba48Version = lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_PATCHCODE_VERSION_IDX];
    // force_lba48_v3: as if the BIOS were v3
    m_FullLba48 = (m_Lba48Version > 3) && !m_ForceLba48V3;

    if (m_Lba48Version == 2)
    {
        partition_table_addr += 0x200;
    }

    m_pKernelTable = (XboxPartitionTable *)partition_table_addr;

    // The kernel's table, in its memory
    memcpy(p_table, (void *)partition_table_addr, sizeof(XboxPartitionTable));

    return true;
}

bool App::reload_lba48()
{
    if (m_pTable != NULL)
    {
        delete (m_pTable);
        m_pTable = NULL;
    }

    if (m_pTableOriginal != NULL)
    {
        delete (m_pTableOriginal);
        m_pTableOriginal = NULL;
    }

    if (read_lba48_info(&m_TableKernel))
    {
        m_pTable = new XboxPartitionTableManipulator(m_TableKernel, m_TotalHddSectors, m_FullLba48, m_Physical4k);
        m_pTableOriginal = new XboxPartitionTableManipulator(m_TableKernel, m_TotalHddSectors, m_FullLba48, m_Physical4k);
        return true;
    }
    else
    {
        m_pTable = new XboxPartitionTableManipulator(m_TotalHddSectors, m_FullLba48, m_Physical4k);
        m_pTableOriginal = new XboxPartitionTableManipulator(m_TotalHddSectors, m_FullLba48, m_Physical4k);
        return false;
    }
}

unsigned int App::get_cluster_size_from_partition(ANSI_STRING part)
{
    HANDLE hDisk;
    NTSTATUS status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    BYTE PartSector[HDD_SECTOR_SIZE * 8];
    LARGE_INTEGER RWOffset;
    FAT_VOLUME_METADATA fsb;

    InitializeObjectAttributes(&ObjectAttributes, &part, OBJ_CASE_INSENSITIVE, NULL);

    RWOffset.QuadPart = 0;
    memset(PartSector, 0, sizeof(PartSector));

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_READ, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);

    if (!NT_SUCCESS(status))
    {
        return 0;
    }

    status = NtReadFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, sizeof(PartSector), &RWOffset);
    NtClose(hDisk);

    if (!NT_SUCCESS(status))
    {
        return 0;
    }

    memcpy(&fsb, PartSector, sizeof(fsb) < sizeof(PartSector) ? sizeof(fsb) : sizeof(PartSector));

    // No FATX header (unformatted, or something else): no cluster size
    if (fsb.Signature != FATX_signature)
    {
        return 0;
    }

    // In sectors, always a power of two
    const uint32_t sectors_per_cluster = fsb.SectorsPerCluster;
    if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0)
    {
        return 0;
    }

    return sectors_per_cluster / 2;
}

void App::RefreshClusterSizes()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        m_ClusterSizes[i] = get_cluster_size_from_partition(m_PartitionPaths[i]);
        m_UsageKnown[i] = false;
    }
}

LARGE_INTEGER App::get_backup_partition_offset_bytes()
{
    // The manipulator decides where the backup goes
    LARGE_INTEGER byte_pos;
    byte_pos.QuadPart = (ULONGLONG)m_pTable->GetBackupTableLba() * (ULONGLONG)HDD_SECTOR_SIZE;
    return byte_pos;
}

// Prepares a table for writing and says whether a backup can be written.
// Returns false if it must not be written; callers explain why beforehand
// (see RefuseUnwritableTable).
bool App::prepare_table(const XboxPartitionTable &table_in, XboxPartitionTable *table, bool *backup_possible)
{
    // Sector 0 says how its LBAs are stored: v4, or v3 (also with force_lba48_v3)
    *table = table_in;
    table->Version = XboxPartitionTableManipulator::VersionFor(m_FullLba48);

    // Without a known disk size the layout can't be trusted
    if (m_TotalHddSectors == 0)
    {
        return false;
    }

    const XboxPartitionTableManipulator to_write(*table, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    int reserved = 0;
    int first = 0;
    int second = 0;
    if (to_write.FindPartitionBeforeUsableStart(&reserved) || to_write.FindConflict(&first, &second))
    {
        return false;
    }

    // No backup without an lba48 patch (the size is the 28 bit one), without
    // room for it, or when the last partition covers its sector
    *backup_possible = (m_pKernelTable != NULL) && (to_write.GetBackupTableLba() != 0) && !to_write.OverlapsBackupTable();
    return true;
}

// A Titan BIOS only reads the backup, so ask whether sector 0 should be
// written too (for Cerbios). False if nothing the BIOS reads can be written.
bool App::ChooseTablesToWrite(bool backup_possible, bool *write_main)
{
    *write_main = true;
    if (!m_TitanBios)
    {
        return true;
    }
    static const LPCWSTR lines[] = {
        L"Titan patched BIOSes only use the backup table.",
        L"Also write it to sector 0, for compatibility with",
        L"Cerbios and other BIOSes that use the table there?",
    };
    *write_main = RunDialog(L"Titan BIOS", COLOR_WHITE, lines, ARRAY_COUNT(lines), L"Sector 0 too", L"Backup only");
    return backup_possible || *write_main;
}

// Writes the tables to disk and to the kernel, without dialogs or editor
// updates, so a worker thread can call it. Reload the editor afterwards on
// the main thread.
bool App::write_tables_raw(const XboxPartitionTable &table_in, bool write_main, bool *backup_written)
{
    *backup_written = false;
    XboxPartitionTable table;
    bool write_backup;
    if (!prepare_table(table_in, &table, &write_backup) || (!write_backup && !write_main))
    {
        return false;
    }

    XboxPartitionTable backup = table;
    if (m_TitanBios)
    {
        backup.Version = XBOX_TABLE_VERSION_TITAN;
    }

    const XboxPartitionTableManipulator to_write(table, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    LARGE_INTEGER RWOffset;
    RWOffset.QuadPart = 0;
    LARGE_INTEGER RWOffset_backup;
    RWOffset_backup.QuadPart = (ULONGLONG)to_write.GetBackupTableLba() * HDD_SECTOR_SIZE;

    BYTE PartSector[HDD_SECTOR_SIZE]; // One sector, see partition.hpp
    BYTE BackupSector[HDD_SECTOR_SIZE];
    memcpy(PartSector, &table, HDD_SECTOR_SIZE);
    memcpy(BackupSector, &backup, HDD_SECTOR_SIZE);

    // From here D: may no longer match its partition
    if (m_LaunchPartition < 0 || MovesLaunchPartition(table))
    {
        m_ThemeSaveBlocked = true;
    }

    HANDLE hDisk = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ANSI_STRING a_path;
    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    IO_STATUS_BLOCK IoStatusBlock;
    // Read and write only: a resize writes the table while its own handle on
    // the disk is open, and more access (GENERIC_ALL) may not be shared
    NTSTATUS status = NtOpenFile(
        &hDisk, SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);

    // Backup first: if it fails nothing has been touched yet
    if (status == 0 && write_backup)
    {
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)BackupSector, HDD_SECTOR_SIZE, &RWOffset_backup);
    }

    if (status == 0 && write_main)
    {
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset);
    }

    // The kernel gets the copy it reads; m_TableKernel catches up in reload_lba48()
    if (status == 0 && m_pKernelTable)
    {
        memcpy(m_pKernelTable, m_TitanBios ? &backup : &table, sizeof(XboxPartitionTable));
    }

    if (hDisk != NULL)
    {
        NtClose(hDisk);
    }

    *backup_written = (status == 0) && write_backup;
    return status == 0;
}

// Refreshes the editor, the on-disk tables and the cluster sizes after a write
void App::reload_after_write(const XboxPartitionTable &written)
{
    if (m_pKernelTable)
    {
        // Rebuilds both editor tables from the updated kernel table
        reload_lba48();
    }
    else
    {
        // No patch: reload_lba48() would fail, so the written table becomes the
        // baseline here. The kernel only sees it after a reboot.
        m_pTable->LoadNewTable(written);
        m_pTableOriginal->LoadNewTable(written);
    }

    // After the editor tables: reading the backup checks m_pTableOriginal
    m_TableHdd = read_hdd_main_partition_table();
    m_TableHddBackup = read_hdd_backup_partition_table();

    m_LoadedTable = LOADED_TABLE_KERNEL;

    // Partitions may now point at different sectors
    RefreshClusterSizes();
}

// The main table and, when possible, the backup (*backup_written)
bool App::write_partition_tables(const XboxPartitionTable table_in, bool *backup_written, bool *main_written)
{
    if (backup_written != NULL)
    {
        *backup_written = false;
    }
    if (main_written != NULL)
    {
        *main_written = false;
    }

    XboxPartitionTable table;
    bool backup_possible;
    bool write_main;
    bool written_backup;
    if (!prepare_table(table_in, &table, &backup_possible) || !ChooseTablesToWrite(backup_possible, &write_main) ||
        !write_tables_raw(table, write_main, &written_backup))
    {
        return false;
    }

    if (backup_written != NULL)
    {
        *backup_written = written_backup;
    }
    if (main_written != NULL)
    {
        *main_written = write_main;
    }
    reload_after_write(table);
    return true;
}

// Built on a copy, so the edits survive a failed write
bool App::erase_partition_tables()
{
    XboxPartitionTableManipulator stock(m_pTable->GetPartitionTable(), m_TotalHddSectors, m_FullLba48, m_Physical4k);
    stock.SetupPartitionsStock();
    return write_partition_tables(stock.GetPartitionTable());
}

XboxPartitionTable App::read_hdd_partition_table(LARGE_INTEGER offset)
{
    // A failed read gives a table without magic
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    BYTE PartSector[HDD_SECTOR_SIZE];
    if (read_hdd_sector((ULONGLONG)offset.QuadPart / HDD_SECTOR_SIZE, PartSector))
    {
        memcpy(&table, PartSector, HDD_SECTOR_SIZE);
    }
    return table;
}

// Reads one sector of the disk
bool App::read_hdd_sector(ULONGLONG lba, BYTE *sector)
{
    LARGE_INTEGER RWOffset;
    RWOffset.QuadPart = lba * HDD_SECTOR_SIZE;

    HANDLE hDisk = NULL;
    NTSTATUS status;
    ANSI_STRING a_path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    memset(sector, 0, HDD_SECTOR_SIZE);

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_READ, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
    if (status == 0)
    {
        status = NtReadFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)sector, HDD_SECTOR_SIZE, &RWOffset);
    }
    if (hDisk != NULL)
    {
        NtClose(hDisk);
    }
    return status == 0;
}

XboxPartitionTable App::read_hdd_main_partition_table()
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    if (m_TotalHddSectors > 0)
    {
        LARGE_INTEGER offset;
        offset.QuadPart = 0;
        table = read_hdd_partition_table(offset);
    }

    return table;
}

XboxPartitionTable App::read_hdd_backup_partition_table()
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    // Without a patch the "last sector" may be mid disk (28 bit size)
    if (m_pTable->GetBackupTableLba() == 0 || m_pKernelTable == NULL)
    {
        m_BackupStatus = BACKUP_NOT_USED;
        return table;
    }

    // The table on disk decides which sectors hold partition data
    const bool backup_overlapped = m_pTableOriginal->OverlapsBackupTable();
    if (!backup_overlapped)
    {
        table = read_hdd_partition_table(get_backup_partition_offset_bytes());
        if (XboxPartitionTableManipulator::HasMagic(table))
        {
            m_BackupStatus = BACKUP_FOUND;
            return table;
        }
        memset(&table, 0, sizeof(table));
    }
    else
    {
        // This sector belongs to a partition, but other tools put a backup
        // here. If it matches the table in use, the block holds no data
        // (see OfferShrinkForBackup()).
        table = read_hdd_partition_table(get_backup_partition_offset_bytes());
        if (XboxPartitionTableManipulator::IsTableUsable(table, m_TotalHddSectors) &&
            XboxPartitionTableManipulator::IsTableUsable(m_TableHdd, m_TotalHddSectors) && same_table_but_version(table, m_TableHdd) &&
            same_table_but_version(table, m_TableKernel))
        {
            m_BackupStatus = BACKUP_OVERLAPPED_MATCH;
            return table;
        }
        memset(&table, 0, sizeof(table));
    }

    // Older builds put the backup in the last sector, also on 512e drives:
    // read only fallback, the next write uses the new place
    m_BackupStatus = backup_overlapped ? BACKUP_OVERLAPPED : BACKUP_MISSING;
    const uint64_t last_sector_lba = m_pTable->GetLastSectorLba();
    if (last_sector_lba != m_pTable->GetBackupTableLba() && !m_pTableOriginal->OverlapsSector(last_sector_lba))
    {
        LARGE_INTEGER legacy_offset;
        legacy_offset.QuadPart = (ULONGLONG)last_sector_lba * (ULONGLONG)HDD_SECTOR_SIZE;
        table = read_hdd_partition_table(legacy_offset);
        if (XboxPartitionTableManipulator::HasMagic(table))
        {
            m_BackupStatus = BACKUP_FOUND_LEGACY;
        }
    }

    return table;
}

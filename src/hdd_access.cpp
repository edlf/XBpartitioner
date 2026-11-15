#include "stdafx.h"
#include "hdd_access.h"
#include <string.h>

#include <winbase.h>
#include "Winioctl.h"

// Passed to the kernel: must match its layout (32 bit pointers)
static_assert(sizeof(ANSI_STRING) == 8, "ANSI_STRING layout is wrong!");
static_assert(sizeof(OBJECT_ATTRIBUTES) == 12, "OBJECT_ATTRIBUTES layout is wrong!");
static_assert(sizeof(IO_STATUS_BLOCK) == 8, "IO_STATUS_BLOCK layout is wrong!");
static_assert(sizeof(DISK_GEOMETRY) == 24, "DISK_GEOMETRY layout is wrong!");

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

static const UCHAR ATA_COMMAND_IDENTIFY_DEVICE = 0xEC;
static const UCHAR ATA_DRIVE_HEAD_MASTER = 0xA0;

// ATA IDENTIFY DEVICE words used here
static const int ATA_ID_FIRMWARE_WORD = 23;            // 4 words, 8 characters
static const int ATA_ID_MODEL_WORD = 27;               // 20 words, 40 characters
static const int ATA_ID_LBA28_SECTORS_WORD = 60;       // 2 words
static const int ATA_ID_COMMAND_SET_2_WORD = 83;       // bit 10: 48 bit addressing supported
static const int ATA_ID_LBA48_SECTORS_WORD = 100;      // 4 words
static const int ATA_ID_SECTOR_SIZE_WORD = 106;        // physical/logical sector size
static const int ATA_ID_DATA_SET_MANAGEMENT_WORD = 169; // Bit 0: TRIM
static const int ATA_ID_ROTATION_RATE_WORD = 217;      // 1: SSD, 0: not reported, else rpm

// Both kernel ioctls here take a command buffer of this many words
static const int IOCTL_BUFFER_WORDS = 100;

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

// 48 bit count if supported, else the 28 bit one
static ULONGLONG identify_native_sectors(const USHORT *identify)
{
    if (identify[ATA_ID_COMMAND_SET_2_WORD] & 0x0400)
    {
        return (ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD] | ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 1] << 16) |
               ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 2] << 32) | ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 3] << 48);
    }
    return (ULONGLONG)identify[ATA_ID_LBA28_SECTORS_WORD] | ((ULONGLONG)identify[ATA_ID_LBA28_SECTORS_WORD + 1] << 16);
}

// Word 106:
//   bits 15:14 = 01  word is valid (else: old drive, 512 byte physical sectors)
//   bit 13         multiple logical sectors per physical sector
//   bits 3:0       log2(logical sectors per physical sector), 3 = 8 = 4K
static unsigned int identify_physical_sector_size(const USHORT *identify)
{
    const USHORT sector_size_info = identify[ATA_ID_SECTOR_SIZE_WORD];
    if ((sector_size_info & 0xC000) == 0x4000 && (sector_size_info & 0x2000) != 0)
    {
        return HDD_SECTOR_SIZE << (sector_size_info & 0x000F);
    }
    return HDD_SECTOR_SIZE;
}

static DriveInfo drive_info_from_identify(const USHORT *identify)
{
    DriveInfo drive;
    memset(&drive, 0, sizeof(drive));
    drive.identify_ok = true;
    ata_string(&identify[ATA_ID_MODEL_WORD], 20, drive.model);
    ata_string(&identify[ATA_ID_FIRMWARE_WORD], 4, drive.firmware);
    drive.native_sectors = identify_native_sectors(identify);
    drive.physical_sector_size = identify_physical_sector_size(identify);
    drive.physical_4k = drive.physical_sector_size >= 4096;
    // This is the drive's answer even through an IDE adapter, which may not
    // pass TRIM on
    drive.rotation_rate = identify[ATA_ID_ROTATION_RATE_WORD];
    drive.trim = (identify[ATA_ID_DATA_SET_MANAGEMENT_WORD] & 0x0001) != 0;
    return drive;
}

NTSTATUS HddAccess::OpenDisk(HANDLE *handle, ACCESS_MASK access)
{
    ANSI_STRING path;
    RtlInitAnsiString(&path, "\\Device\\Harddisk0\\Partition0");
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, NULL);
    IO_STATUS_BLOCK status_block;
    return NtOpenFile(handle, access, &attributes, &status_block, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
}

ULONGLONG HddAccess::ReadSizeInSectors()
{
    HANDLE handle;
    if (!NT_SUCCESS(OpenDisk(&handle, SYNCHRONIZE | GENERIC_READ)))
    {
        // Unknown size (not the NTSTATUS, which would read as a sector count)
        return 0;
    }

    unsigned int command[IOCTL_BUFFER_WORDS];
    memset(command, 0, sizeof(command));
    command[0] = IOCTL_SUBCMD_GET_INFO;

    DISK_GEOMETRY geometry;
    IO_STATUS_BLOCK status_block;
    const NTSTATUS status = NtDeviceIoControlFile(
        handle, 0, 0, 0, &status_block, IOCTL_DISK_GET_DRIVE_GEOMETRY, command, sizeof(command), &geometry, sizeof(geometry));
    NtClose(handle);

    return NT_SUCCESS(status) ? geometry.Cylinders.QuadPart : 0;
}

DriveInfo HddAccess::ReadIdentify()
{
    DriveInfo failed;
    memset(&failed, 0, sizeof(failed));

    HANDLE handle;
    if (!NT_SUCCESS(OpenDisk(&handle, SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE)))
    {
        return failed;
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

    IO_STATUS_BLOCK status_block;
    const NTSTATUS status = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &status_block, IOCTL_IDE_PASS_THROUGH, &ata, sizeof(ata), &ata, sizeof(ata));
    NtClose(handle);

    return NT_SUCCESS(status) ? drive_info_from_identify(identify) : failed;
}

Lba48Patch HddAccess::ReadLba48Patch()
{
    Lba48Patch patch = {false, 0, NULL};

    HANDLE handle;
    if (!NT_SUCCESS(OpenDisk(&handle, SYNCHRONIZE | GENERIC_READ)))
    {
        return patch;
    }

    unsigned int command[IOCTL_BUFFER_WORDS];
    unsigned int answer[IOCTL_BUFFER_WORDS];
    memset(command, 0, sizeof(command));
    memset(answer, 0, sizeof(answer));
    command[0] = IOCTL_SUBCMD_GET_INFO;

    IO_STATUS_BLOCK status_block;
    const NTSTATUS status =
        NtDeviceIoControlFile(handle, 0, 0, 0, &status_block, IOCTL_CMD_LBA48_ACCESS, command, sizeof(command), answer, sizeof(answer));
    NtClose(handle);

    if (!NT_SUCCESS(status) || answer[LBA48_GET_INFO_MAGIC1_IDX] != LBA48_GET_INFO_MAGIC1_VAL ||
        answer[LBA48_GET_INFO_MAGIC2_IDX] != LBA48_GET_INFO_MAGIC2_VAL)
    {
        return patch;
    }

    // Compatible with Paul's lba48 patched kernels
    unsigned int table_address = answer[LBA48_GET_INFO_LOWCODE_BASE_IDX] + answer[LBA48_GET_INFO_PART_TABLE_OFS_IDX];
    patch.version = answer[LBA48_GET_INFO_PATCHCODE_VERSION_IDX];
    if (patch.version == 2)
    {
        table_address += 0x200;
    }
    patch.kernel_table = (XboxPartitionTable *)table_address;
    patch.present = true;
    return patch;
}

bool HddAccess::ReadSector(ULONGLONG lba, BYTE *sector)
{
    memset(sector, 0, HDD_SECTOR_SIZE);

    HANDLE handle;
    if (!NT_SUCCESS(OpenDisk(&handle, SYNCHRONIZE | GENERIC_READ)))
    {
        return false;
    }

    LARGE_INTEGER offset;
    offset.QuadPart = lba * HDD_SECTOR_SIZE;
    IO_STATUS_BLOCK status_block;
    const NTSTATUS status = NtReadFile(handle, NULL, NULL, NULL, &status_block, (PVOID)sector, HDD_SECTOR_SIZE, &offset);
    NtClose(handle);
    return TransferredAll(status, status_block, HDD_SECTOR_SIZE);
}

XboxPartitionTable HddAccess::ReadTableAt(ULONGLONG lba)
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    BYTE sector[HDD_SECTOR_SIZE];
    if (ReadSector(lba, sector))
    {
        memcpy(&table, sector, HDD_SECTOR_SIZE);
    }
    return table;
}

static bool write_table_sector(HANDLE disk, const XboxPartitionTable &table, ULONGLONG lba)
{
    BYTE sector[HDD_SECTOR_SIZE]; // One sector, see partition.hpp
    memcpy(sector, &table, HDD_SECTOR_SIZE);
    LARGE_INTEGER offset;
    offset.QuadPart = lba * HDD_SECTOR_SIZE;
    IO_STATUS_BLOCK status_block;
    const NTSTATUS status = NtWriteFile(disk, NULL, NULL, NULL, &status_block, (PVOID)sector, HDD_SECTOR_SIZE, &offset);
    return TransferredAll(status, status_block, HDD_SECTOR_SIZE);
}

TableSectorsWritten HddAccess::WriteTableSectors(const TableSectorWrite &write)
{
    TableSectorsWritten written = {false, false};

    // Read and write only: a resize writes the table while its own handle on
    // the disk is open, and more access (GENERIC_ALL) may not be shared
    HANDLE handle;
    if (!NT_SUCCESS(OpenDisk(&handle, SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE)))
    {
        return written;
    }

    // The backup first: if it fails, sector 0 is left alone
    written.backup = write.backup != NULL && write_table_sector(handle, *write.backup, write.backup_lba);
    if (written.backup || write.backup == NULL)
    {
        written.main = write.main != NULL && write_table_sector(handle, *write.main, 0);
    }
    NtClose(handle);
    return written;
}

unsigned int HddAccess::ReadClusterSizeKb(const ANSI_STRING &partition_path)
{
    ANSI_STRING path = partition_path;
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, NULL);

    HANDLE handle;
    IO_STATUS_BLOCK status_block;
    NTSTATUS status = NtOpenFile(&handle, SYNCHRONIZE | GENERIC_READ, &attributes, &status_block, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(status))
    {
        return 0;
    }

    BYTE first_sectors[HDD_SECTOR_SIZE * 8];
    memset(first_sectors, 0, sizeof(first_sectors));
    LARGE_INTEGER offset;
    offset.QuadPart = 0;
    status = NtReadFile(handle, NULL, NULL, NULL, &status_block, (PVOID)first_sectors, sizeof(first_sectors), &offset);
    NtClose(handle);
    if (!NT_SUCCESS(status))
    {
        return 0;
    }

    FAT_VOLUME_METADATA header;
    memcpy(&header, first_sectors, sizeof(header) < sizeof(first_sectors) ? sizeof(header) : sizeof(first_sectors));

    // No FATX header (unformatted, or something else): no cluster size
    if (header.Signature != FATX_signature)
    {
        return 0;
    }

    // 0 for an odd value
    return FatxClusterBytes(header.SectorsPerCluster, FATX_MEDIUM_HARD_DRIVE) / 1024;
}

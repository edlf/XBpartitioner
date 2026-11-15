#ifndef DISK_PARTITION_DEVICE_H_
#define DISK_PARTITION_DEVICE_H_

#include <xtl.h>
#include "Undocumented.h"
#include "fatx_check.hpp"
#include "hdd_access.h"

// One partition, accessed through the whole-disk device
class DiskPartitionDevice : public FatxDevice
{
  public:
    DiskPartitionDevice(ULONGLONG start_lba, bool writable) : m_base(start_lba * HDD_SECTOR_SIZE), m_disk(NULL), m_writable(writable)
    {
        ANSI_STRING path;
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK status_block;
        RtlInitAnsiString(&path, "\\Device\\Harddisk0\\Partition0");
        InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE, NULL);
        const ACCESS_MASK access = SYNCHRONIZE | GENERIC_READ | (writable ? GENERIC_WRITE : 0);
        if (!NT_SUCCESS(NtOpenFile(&m_disk, access, &attributes, &status_block, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT)))
        {
            m_disk = NULL;
        }
    }

    ~DiskPartitionDevice()
    {
        if (m_disk != NULL)
        {
            NtClose(m_disk);
        }
    }

    bool Read(uint64_t offset, void *buffer, uint32_t length)
    {
        if (m_disk == NULL)
        {
            return false;
        }
        LARGE_INTEGER position;
        position.QuadPart = m_base + offset;
        IO_STATUS_BLOCK status_block;
        const NTSTATUS status = NtReadFile(m_disk, NULL, NULL, NULL, &status_block, buffer, length, &position);
        return TransferredAll(status, status_block, length);
    }

    // The Xbox's own hard drive
    FatxMedium Medium() const
    {
        return FATX_MEDIUM_HARD_DRIVE;
    }

    bool Write(uint64_t offset, const void *buffer, uint32_t length)
    {
        if (m_disk == NULL || !m_writable)
        {
            return false;
        }
        LARGE_INTEGER position;
        position.QuadPart = m_base + offset;
        IO_STATUS_BLOCK status_block;
        const NTSTATUS status = NtWriteFile(m_disk, NULL, NULL, NULL, &status_block, (PVOID)buffer, length, &position);
        return TransferredAll(status, status_block, length);
    }

    // Past the drive's write cache, so the resize journal's order holds after
    // a power cut. Nothing to flush on a read only device.
    bool Sync()
    {
        if (!m_writable)
        {
            return true;
        }
        if (m_disk == NULL)
        {
            return false;
        }
        IO_STATUS_BLOCK status_block;
        return NT_SUCCESS(NtFlushBuffersFile(m_disk, &status_block));
    }

  private:
    ULONGLONG m_base;
    HANDLE m_disk;
    bool m_writable;
};

// The kernel's device for a partition table index, e.g. \Device\Harddisk0\Partition6.
// Points into static text, so it can be kept. Empty (nothing opens) for an
// index outside the table.
ANSI_STRING PartitionDevicePath(int partition);
// So the kernel mounts it again with its new size or file system
NTSTATUS DismountPartition(int partition);

#endif // DISK_PARTITION_DEVICE_H_

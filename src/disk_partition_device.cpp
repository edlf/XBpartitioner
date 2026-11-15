#include "stdafx.h"
#include "disk_partition_device.h"

// Kernel device of each table index; the paths handed out point into these
static const char *const PARTITION_DEVICE_PATHS[XboxPartitionTableEntryCount] = {
    "\\Device\\Harddisk0\\Partition1",
    "\\Device\\Harddisk0\\Partition2",
    "\\Device\\Harddisk0\\Partition3",
    "\\Device\\Harddisk0\\Partition4",
    "\\Device\\Harddisk0\\Partition5",
    "\\Device\\Harddisk0\\Partition6",
    "\\Device\\Harddisk0\\Partition7",
    "\\Device\\Harddisk0\\Partition8",
    "\\Device\\Harddisk0\\Partition9",
    "\\Device\\Harddisk0\\Partition10",
    "\\Device\\Harddisk0\\Partition11",
    "\\Device\\Harddisk0\\Partition12",
    "\\Device\\Harddisk0\\Partition13",
    "\\Device\\Harddisk0\\Partition14",
};

// STATUS_INVALID_PARAMETER, which the XDK headers don't define
static const NTSTATUS INVALID_PARTITION_STATUS = (NTSTATUS)0xC000000DL;

static bool is_partition_index(int partition)
{
    return partition >= 0 && partition < (int)XboxPartitionTableEntryCount;
}

ANSI_STRING PartitionDevicePath(int partition)
{
    ANSI_STRING path;
    RtlInitAnsiString(&path, is_partition_index(partition) ? PARTITION_DEVICE_PATHS[partition] : "");
    return path;
}

NTSTATUS DismountPartition(int partition)
{
    if (!is_partition_index(partition))
    {
        return INVALID_PARTITION_STATUS;
    }
    ANSI_STRING path = PartitionDevicePath(partition);
    return IoDismountVolumeByName(&path);
}

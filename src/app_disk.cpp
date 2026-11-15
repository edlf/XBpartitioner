// What App reads for its FilesystemCache (FilesystemReader).

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

unsigned int App::ReadClusterSizeKb(int partition)
{
    return m_hdd.ReadClusterSizeKb(PartitionDevicePath(partition));
}

// Only partitions in the table on disk are read
bool App::ReadUsage(int partition, FatxUsage *usage)
{
    return m_session.InUse().IsPartitionEnabled(partition) && m_fsck.ReadUsage(partition, usage);
}

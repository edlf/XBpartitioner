#include "filesystem_cache.h"

FilesystemCache::FilesystemCache(FilesystemReader &reader) : m_reader(reader)
{
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        m_cluster_sizes_kb[i] = 0;
    }
}

void FilesystemCache::RefreshAll()
{
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        Refresh(i);
    }
}

void FilesystemCache::Refresh(int partition)
{
    if (!IsIndex(partition))
    {
        return;
    }
    m_cluster_sizes_kb[partition] = m_reader.ReadClusterSizeKb(partition);
    m_usage[partition].Forget();
}

unsigned int FilesystemCache::ClusterSizeKb(int partition) const
{
    return IsIndex(partition) ? m_cluster_sizes_kb[partition] : 0;
}

const PartitionUsage &FilesystemCache::Usage(int partition) const
{
    static const PartitionUsage unknown;
    return IsIndex(partition) ? m_usage[partition] : unknown;
}

bool FilesystemCache::EnsureUsage(int partition)
{
    if (!IsIndex(partition))
    {
        return false;
    }
    if (m_usage[partition].IsKnown())
    {
        return true;
    }
    if (m_cluster_sizes_kb[partition] == 0)
    {
        return false;
    }

    FatxUsage usage;
    if (!m_reader.ReadUsage(partition, &usage))
    {
        return false;
    }
    m_usage[partition].Remember(usage);
    return true;
}

bool FilesystemCache::IsIndex(int partition)
{
    return partition >= 0 && partition < (int)XboxPartitionTableEntryCount;
}

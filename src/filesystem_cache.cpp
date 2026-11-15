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
    m_cluster_sizes_kb[partition] = m_reader.ReadClusterSizeKb(partition);
    m_usage[partition].Forget();
}

unsigned int FilesystemCache::ClusterSizeKb(int partition) const
{
    return m_cluster_sizes_kb[partition];
}

const PartitionUsage &FilesystemCache::Usage(int partition) const
{
    return m_usage[partition];
}

bool FilesystemCache::EnsureUsage(int partition)
{
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

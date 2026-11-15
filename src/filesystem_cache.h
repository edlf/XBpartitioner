#ifndef FILESYSTEM_CACHE_H_
#define FILESYSTEM_CACHE_H_

#include "partition.hpp"
#include "partition_usage.h"

// Reads a partition's file system as it is on disk (the Xbox app reads
// through the kernel; tests use a fake)
class FilesystemReader
{
  public:
    virtual ~FilesystemReader()
    {
    }
    // FATX cluster size in KB, 0 if not formatted or unreadable
    virtual unsigned int ReadClusterSizeKb(int partition) = 0;
    // Scans the FAT. False if it can't be read.
    virtual bool ReadUsage(int partition, FatxUsage *usage) = 0;
};

// What each partition's file system looks like, so the screens don't reopen
// every partition: the cluster size, read for all at once, and the usage,
// read the first time it's needed (scanning a FAT is slow)
class FilesystemCache
{
  public:
    explicit FilesystemCache(FilesystemReader &reader);

    // After the tables change: partitions may point at other sectors
    void RefreshAll();
    // After formatting one
    void Refresh(int partition);

    // 0 if not formatted
    unsigned int ClusterSizeKb(int partition) const;
    const PartitionUsage &Usage(int partition) const;
    // Reads the usage of a formatted partition unless it's known. False if
    // it isn't formatted or can't be read.
    bool EnsureUsage(int partition);

  private:
    // Out of range indexes read as unformatted and are never read from disk
    static bool IsIndex(int partition);

    FilesystemReader &m_reader;
    unsigned int m_cluster_sizes_kb[XboxPartitionTableEntryCount];
    PartitionUsage m_usage[XboxPartitionTableEntryCount];
};

#endif // FILESYSTEM_CACHE_H_

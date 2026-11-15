// FilesystemCache: when the Xbox app reads a partition's cluster size and
// usage, and when it reuses what it read

#include <gtest/gtest.h>
#include "filesystem_cache.h"

namespace
{

const int FORMATTED = 5;
const int UNFORMATTED = 6;
const unsigned int CLUSTER_KB = 16;

// Partition FORMATTED has 16 KB clusters; usage reads count and can fail
class FakeReader : public FilesystemReader
{
  public:
    FakeReader() : usage_reads(0), usage_fails(false)
    {
    }

    unsigned int ReadClusterSizeKb(int partition)
    {
        return partition == FORMATTED ? CLUSTER_KB : 0;
    }

    bool ReadUsage(int partition, FatxUsage *usage)
    {
        usage_reads++;
        const FatxUsage read = {CLUSTER_KB * 1024, 100 + (uint64_t)partition, 0, 200};
        *usage = read;
        return !usage_fails;
    }

    int usage_reads;
    bool usage_fails;
};

class FilesystemCacheTest : public testing::Test
{
  protected:
    FilesystemCacheTest() : m_cache(m_reader)
    {
        m_cache.RefreshAll();
    }

    FakeReader m_reader;
    FilesystemCache m_cache;
};

} // namespace

TEST(FilesystemCache, KnowsNothingBeforeTheFirstRefresh)
{
    FakeReader reader;
    const FilesystemCache cache(reader);
    EXPECT_EQ(cache.ClusterSizeKb(FORMATTED), 0u);
}

TEST_F(FilesystemCacheTest, RefreshReadsClusterSizes)
{
    EXPECT_EQ(m_cache.ClusterSizeKb(FORMATTED), CLUSTER_KB);
}

TEST_F(FilesystemCacheTest, UnformattedPartitionHasNoClusterSize)
{
    EXPECT_EQ(m_cache.ClusterSizeKb(UNFORMATTED), 0u);
}

TEST_F(FilesystemCacheTest, UsageIsNotReadUpFront)
{
    EXPECT_EQ(m_reader.usage_reads, 0);
}

TEST_F(FilesystemCacheTest, EnsureUsageReadsAFormattedPartition)
{
    EXPECT_TRUE(m_cache.EnsureUsage(FORMATTED));
}

TEST_F(FilesystemCacheTest, EnsuredUsageIsKnown)
{
    m_cache.EnsureUsage(FORMATTED);
    EXPECT_EQ(m_cache.Usage(FORMATTED).Get().used_clusters, 100u + FORMATTED);
}

TEST_F(FilesystemCacheTest, KnownUsageIsNotReadAgain)
{
    m_cache.EnsureUsage(FORMATTED);
    m_cache.EnsureUsage(FORMATTED);
    EXPECT_EQ(m_reader.usage_reads, 1);
}

TEST_F(FilesystemCacheTest, UnformattedPartitionIsNotScanned)
{
    m_cache.EnsureUsage(UNFORMATTED);
    EXPECT_EQ(m_reader.usage_reads, 0);
}

TEST_F(FilesystemCacheTest, FailedReadLeavesUsageUnknown)
{
    m_reader.usage_fails = true;
    m_cache.EnsureUsage(FORMATTED);
    EXPECT_FALSE(m_cache.Usage(FORMATTED).IsKnown());
}

TEST_F(FilesystemCacheTest, RefreshForgetsUsage)
{
    m_cache.EnsureUsage(FORMATTED);
    m_cache.Refresh(FORMATTED);
    EXPECT_FALSE(m_cache.Usage(FORMATTED).IsKnown());
}

TEST_F(FilesystemCacheTest, RefreshOfOnePartitionKeepsTheOthers)
{
    m_cache.EnsureUsage(FORMATTED);
    m_cache.Refresh(UNFORMATTED);
    EXPECT_TRUE(m_cache.Usage(FORMATTED).IsKnown());
}

TEST_F(FilesystemCacheTest, IndexOutsideTheTableReadsAsUnformatted)
{
    EXPECT_EQ(m_cache.ClusterSizeKb(-1), 0u);
    EXPECT_FALSE(m_cache.EnsureUsage((int)XboxPartitionTableEntryCount));
    EXPECT_FALSE(m_cache.Usage(-1).IsKnown());
}

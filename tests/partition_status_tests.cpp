// DescribePartitionStatus: the editor's "Status" line for one partition

#include <gtest/gtest.h>
#include <wchar.h>
#include "partition_status.h"

namespace
{

const uint64_t SECTORS_PER_MB = 0x800;
const uint64_t DISK_128GB = 0x10000000;
const uint32_t CLUSTER_BYTES = 16 * 1024;
const int F = XBOX_STANDARD_PARTITION_COUNT;
const int G = F + 1;

FatxUsage UsageUpTo(uint64_t highest_cluster)
{
    FatxUsage usage = {CLUSTER_BYTES, 10, 0, highest_cluster};
    return usage;
}

bool StartsWith(const wchar_t *text, const wchar_t *prefix)
{
    return wcsncmp(text, prefix, wcslen(prefix)) == 0;
}

// The stock partitions plus a 90 MB F on a 128 GB drive, in use and edited
class PartitionStatusTest : public testing::Test
{
  protected:
    PartitionStatusTest() : m_in_use(DISK_128GB, true, false), m_edited(DISK_128GB, true, false), m_resize_keeps_data(true)
    {
        m_in_use.SetupPartitionsFOnly();
        m_in_use.SetPartitionSize(F, 90 * SECTORS_PER_MB);
        m_edited.LoadNewTable(m_in_use.GetPartitionTable());
        m_usage.Remember(UsageUpTo(5000));
    }

    PartitionStatus StatusOf(int partition) const
    {
        const PartitionStatusQuery query = {m_edited, m_in_use, m_usage, m_resize_keeps_data};
        return DescribePartitionStatus(query, partition);
    }

    void AddG()
    {
        m_edited.SetPartitionStart(G, m_in_use.GetPartitionStart(F) + m_in_use.GetPartitionSize(F));
        m_edited.SetPartitionSize(G, 100 * SECTORS_PER_MB);
        m_edited.EnablePartition(G);
    }

    PartitionTableEditor m_in_use;
    PartitionTableEditor m_edited;
    PartitionUsage m_usage;
    bool m_resize_keeps_data;
};

} // namespace

TEST_F(PartitionStatusTest, UnchangedPartitionKeepsItsData)
{
    EXPECT_STREQ(StatusOf(F).text, L"unchanged");
}

TEST_F(PartitionStatusTest, UnchangedIsGreen)
{
    EXPECT_EQ(StatusOf(F).tone, STATUS_TONE_KEEPS);
}

TEST_F(PartitionStatusTest, UnusedSlotIsDisabled)
{
    EXPECT_EQ(StatusOf(G).tone, STATUS_TONE_OFF);
}

TEST_F(PartitionStatusTest, AddedPartitionIsNew)
{
    AddG();
    EXPECT_STREQ(StatusOf(G).text, L"new, format it with write and format");
}

TEST_F(PartitionStatusTest, DisabledPartitionWillBeRemoved)
{
    m_edited.DisablePartition(F);
    EXPECT_STREQ(StatusOf(F).text, L"will be removed when written");
}

TEST_F(PartitionStatusTest, MovedPartitionNeedsAFormat)
{
    m_edited.SetPartitionStart(F, m_in_use.GetPartitionStart(F) + SECTORS_PER_MB);
    EXPECT_STREQ(StatusOf(F).text, L"changed, unreadable until formatted");
}

TEST_F(PartitionStatusTest, ShrinkKeepsDataWhenTheEndIsFree)
{
    m_edited.SetPartitionSize(F, 80 * SECTORS_PER_MB);
    EXPECT_STREQ(StatusOf(F).text, L"resized, keeps its data");
}

TEST_F(PartitionStatusTest, ShrinkThatKeepsDataIsGreen)
{
    m_edited.SetPartitionSize(F, 80 * SECTORS_PER_MB);
    EXPECT_EQ(StatusOf(F).tone, STATUS_TONE_KEEPS);
}

TEST_F(PartitionStatusTest, ShrinkOverUsedClustersSuggestsASize)
{
    m_edited.SetPartitionSize(F, 70 * SECTORS_PER_MB);
    EXPECT_TRUE(StartsWith(StatusOf(F).text, L"format needed (data kept: "));
}

TEST_F(PartitionStatusTest, ResizeFormatsWithTheSettingOff)
{
    m_resize_keeps_data = false;
    m_edited.SetPartitionSize(F, 80 * SECTORS_PER_MB);
    EXPECT_STREQ(StatusOf(F).text, L"changed, unreadable until formatted");
}

TEST_F(PartitionStatusTest, ResizeFormatsWithoutKnownUsage)
{
    m_usage.Forget();
    m_edited.SetPartitionSize(F, 80 * SECTORS_PER_MB);
    EXPECT_STREQ(StatusOf(F).text, L"changed, unreadable until formatted");
}

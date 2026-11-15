// PartitionTableChanges: what the Xbox app treats as changed between the
// edited table and the one on disk (what gets formatted, what keeps its data)

#include <gtest/gtest.h>
#include "partition_table_changes.h"

namespace
{

const uint64_t GB = 0x200000; // sectors
const uint64_t DISK_128GB = 0x10000000;
const int F = XBOX_STANDARD_PARTITION_COUNT; // First extended partition
const int E = 0;

// The stock layout plus F on the rest of a 128 GB drive, edited and on disk
class PartitionTableChangesTest : public testing::Test
{
  protected:
    PartitionTableChangesTest() : m_edited(DISK_128GB, true, false), m_original(DISK_128GB, true, false), m_changes(m_edited, m_original)
    {
        m_original.SetupPartitionsFOnly();
        m_edited.LoadNewTable(m_original.GetPartitionTable());
    }

    void ShrinkF()
    {
        m_edited.SetPartitionSize(F, m_original.GetPartitionSize(F) - GB);
    }

    void GrowE()
    {
        m_edited.SetPartitionSize(E, m_original.GetPartitionSize(E) + GB);
    }

    void MoveFStart()
    {
        m_edited.SetPartitionStart(F, m_original.GetPartitionStart(F) + GB);
    }

    int AddPartition()
    {
        ShrinkF();
        const int added = F + 1;
        m_edited.SetPartitionStart(added, m_original.GetPartitionStart(F) + m_edited.GetPartitionSize(F));
        m_edited.SetPartitionSize(added, GB);
        m_edited.EnablePartition(added);
        return added;
    }

    PartitionTableEditor m_edited;
    PartitionTableEditor m_original;
    PartitionTableChanges m_changes;
};

PartitionSet FAndE()
{
    PartitionSet set = PartitionSet::Only(F);
    set.Add(E);
    return set;
}

} // namespace

TEST_F(PartitionTableChangesTest, UnchangedTableHasNoChanges)
{
    EXPECT_FALSE(m_changes.Any());
}

TEST_F(PartitionTableChangesTest, ResizeIsAChange)
{
    ShrinkF();
    EXPECT_TRUE(m_changes.Any());
}

TEST_F(PartitionTableChangesTest, DisablingIsAChange)
{
    m_edited.DisablePartition(F);
    EXPECT_TRUE(m_changes.Any());
}

TEST_F(PartitionTableChangesTest, AddedPartitionIsAdded)
{
    const int added = AddPartition();
    EXPECT_TRUE(m_changes.IsAdded(added));
}

TEST_F(PartitionTableChangesTest, ExistingPartitionIsNotAdded)
{
    EXPECT_FALSE(m_changes.IsAdded(F));
}

TEST_F(PartitionTableChangesTest, MovedStartIsMovedOrResized)
{
    MoveFStart();
    EXPECT_TRUE(m_changes.IsMovedOrResized(F));
}

TEST_F(PartitionTableChangesTest, DisabledPartitionIsNotMovedOrResized)
{
    m_edited.DisablePartition(F);
    EXPECT_FALSE(m_changes.IsMovedOrResized(F));
}

TEST_F(PartitionTableChangesTest, DisabledPartitionIsAffected)
{
    m_edited.DisablePartition(F);
    EXPECT_TRUE(m_changes.Affects(F));
}

TEST_F(PartitionTableChangesTest, AddedPartitionIsNotAffected)
{
    const int added = AddPartition();
    EXPECT_FALSE(m_changes.Affects(added));
}

TEST_F(PartitionTableChangesTest, NoPartitionIndexIsNeverAffected)
{
    EXPECT_FALSE(m_changes.Affects(-1));
}

TEST_F(PartitionTableChangesTest, ShrinkAtTheSameStartIsResizedAtSameStart)
{
    ShrinkF();
    EXPECT_TRUE(m_changes.IsResizedAtSameStart(F));
}

TEST_F(PartitionTableChangesTest, MoveIsNotResizedAtSameStart)
{
    MoveFStart();
    EXPECT_FALSE(m_changes.IsResizedAtSameStart(F));
}

TEST_F(PartitionTableChangesTest, ChangedIncludesAddedPartitions)
{
    const int added = AddPartition();
    EXPECT_TRUE(m_changes.Changed(PartitionSet()).Contains(added));
}

TEST_F(PartitionTableChangesTest, ChangedLeavesOutKeptPartitions)
{
    ShrinkF();
    EXPECT_TRUE(m_changes.Changed(PartitionSet::Only(F)).IsEmpty());
}

TEST_F(PartitionTableChangesTest, MovedOrResizedLeavesOutAddedPartitions)
{
    const int added = AddPartition();
    EXPECT_FALSE(m_changes.MovedOrResized(PartitionSet()).Contains(added));
}

TEST_F(PartitionTableChangesTest, ShrinkingPicksTheSmallerOne)
{
    ShrinkF();
    GrowE();
    EXPECT_EQ(m_changes.Shrinking(FAndE()).Count(), 1);
}

TEST_F(PartitionTableChangesTest, ShrinkingIsF)
{
    ShrinkF();
    GrowE();
    EXPECT_TRUE(m_changes.Shrinking(FAndE()).Contains(F));
}

TEST_F(PartitionTableChangesTest, GrowingIsE)
{
    ShrinkF();
    GrowE();
    EXPECT_TRUE(m_changes.Growing(FAndE()).Contains(E));
}

TEST_F(PartitionTableChangesTest, GrowingOnlyLooksAtTheGivenPartitions)
{
    GrowE();
    EXPECT_TRUE(m_changes.Growing(PartitionSet::Only(F)).IsEmpty());
}

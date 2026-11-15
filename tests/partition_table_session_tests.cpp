// PartitionTableSession: the editor's table against the table in use, loading
// tables and presets, and undo

#include <gtest/gtest.h>
#include "partition_table_session.h"

namespace
{

const uint64_t GB = 0x200000; // sectors
const uint64_t DISK_128GB = 0x10000000;
const int F = XBOX_STANDARD_PARTITION_COUNT;

PartitionTableEditor StockOn128Gb()
{
    return PartitionTableEditor(DISK_128GB, true, false);
}

XboxPartitionTable FOnlyTable()
{
    PartitionTableEditor table = StockOn128Gb();
    table.SetupPartitionsFOnly();
    return table.GetPartitionTable();
}

class PartitionTableSessionTest : public testing::Test
{
  protected:
    PartitionTableSessionTest()
    {
        m_session.Start(StockOn128Gb());
    }

    PartitionTableSession m_session;
};

} // namespace

TEST(PartitionTableSession, CanBeMadeBeforeTheDiskIsKnown)
{
    const PartitionTableSession session;
    EXPECT_FALSE(session.Changes().Any());
}

TEST_F(PartitionTableSessionTest, StartsWithoutChanges)
{
    EXPECT_FALSE(m_session.Changes().Any());
}

TEST_F(PartitionTableSessionTest, StartsFromTheKernel)
{
    EXPECT_EQ(m_session.Source(), LOADED_TABLE_KERNEL);
}

TEST_F(PartitionTableSessionTest, EditingTheTableIsAChange)
{
    m_session.Edited().SetPartitionSize(0, m_session.Edited().GetPartitionSize(0) - GB);
    EXPECT_TRUE(m_session.Changes().Any());
}

TEST_F(PartitionTableSessionTest, EditingLeavesTheTableInUseAlone)
{
    const uint64_t size = m_session.InUse().GetPartitionSize(0);
    m_session.Edited().SetPartitionSize(0, size - GB);
    EXPECT_EQ(m_session.InUse().GetPartitionSize(0), size);
}

TEST_F(PartitionTableSessionTest, LoadRemembersTheSource)
{
    m_session.Load(FOnlyTable(), LOADED_TABLE_HDD_BACKUP);
    EXPECT_EQ(m_session.Source(), LOADED_TABLE_HDD_BACKUP);
}

TEST_F(PartitionTableSessionTest, UndoBringsBackTheTableInUse)
{
    m_session.Load(FOnlyTable(), LOADED_TABLE_HDD);
    m_session.UndoAllChanges();
    EXPECT_FALSE(m_session.Changes().Any());
}

TEST_F(PartitionTableSessionTest, UndoSourceIsTheKernel)
{
    m_session.Load(FOnlyTable(), LOADED_TABLE_HDD);
    m_session.UndoAllChanges();
    EXPECT_EQ(m_session.Source(), LOADED_TABLE_KERNEL);
}

TEST_F(PartitionTableSessionTest, PresetChangesTheLayout)
{
    m_session.ApplyPreset(PRESET_F);
    EXPECT_TRUE(m_session.Edited().IsPartitionEnabled(F));
}

TEST_F(PartitionTableSessionTest, PresetIsTheSource)
{
    m_session.ApplyPreset(PRESET_F);
    EXPECT_EQ(m_session.Source(), LOADED_TABLE_PRESET);
}

TEST_F(PartitionTableSessionTest, SameLayoutPresetKeepsTheSource)
{
    m_session.ApplyPreset(PRESET_STOCK);
    EXPECT_EQ(m_session.Source(), LOADED_TABLE_KERNEL);
}

TEST_F(PartitionTableSessionTest, RestartMakesTheWrittenTableTheOneInUse)
{
    m_session.Load(FOnlyTable(), LOADED_TABLE_PRESET);
    m_session.Restart(m_session.Edited().GetPartitionTable());
    EXPECT_TRUE(m_session.InUse().IsPartitionEnabled(F) && !m_session.Changes().Any());
}

TEST_F(PartitionTableSessionTest, EmptyPartitionIsDisabled)
{
    m_session.Edited().SetPartitionSize(0, 0);
    m_session.DisableIfEmpty(0);
    EXPECT_FALSE(m_session.Edited().IsPartitionEnabled(0));
}

TEST_F(PartitionTableSessionTest, PartitionWithASizeStaysEnabled)
{
    m_session.DisableIfEmpty(0);
    EXPECT_TRUE(m_session.Edited().IsPartitionEnabled(0));
}

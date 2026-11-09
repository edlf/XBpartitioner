// Google Test tests for partition.cpp, no XDK needed: tests\run_tests.bat
// or tests\PartitionTests.sln.

#include <gtest/gtest.h>
#include "partition.hpp"

namespace
{

const uint64_t GB = 0x200000;              // sectors
const uint64_t DISK_1TB = 1953525168ULL;    // a real 1TB drive
const uint64_t DISK_128GB = 0x10000000;
const uint64_t DISK_40TB = 40ULL * 1024 * 1024 * 1024 * 2;
const uint64_t HUGE_DELTA = 0xFFFFFFFFFFULL;

uint64_t PartEnd(const XboxPartitionTableManipulator &t, int part)
{
    return t.GetPartitionStart(part) + t.GetPartitionSize(part);
}

// First sector partitions may not reach (where the backup table is)
uint64_t LastUsable(const XboxPartitionTableManipulator &t)
{
    return t.GetUsableLBAs() + XBOX_SWAPPART1_LBA_START;
}

bool IsAligned(uint64_t value)
{
    return (value % LBA_ALIGNMENT) == 0;
}

bool HasConflict(const XboxPartitionTableManipulator &t)
{
    int a = -1, b = -1;
    return t.FindConflict(&a, &b);
}

// What presets and the editor make must be writable: aligned, inside the
// usable area, no overlaps
void ExpectSane(const XboxPartitionTableManipulator &t)
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!t.isPartitionEnabled(i))
        {
            continue;
        }
        EXPECT_TRUE(IsAligned(t.GetPartitionStart(i))) << "partition " << i + 1;
        EXPECT_TRUE(IsAligned(t.GetPartitionSize(i))) << "partition " << i + 1;
        EXPECT_GE(t.GetPartitionStart(i), (uint64_t)XBOX_SWAPPART1_LBA_START) << "partition " << i + 1;
        EXPECT_LE(PartEnd(t, i), LastUsable(t)) << "partition " << i + 1 << " ends past the usable area";
    }
    EXPECT_FALSE(HasConflict(t));
}

// Stock layout plus F at the first free spot, 1M sectors
void SetupWithF(XboxPartitionTableManipulator &t)
{
    t.SetupPartitionsStock();
    t.EnablePartition(5);
    t.SetPartitionSize(5, 0x100000);
}

} // namespace

TEST(Align, Helpers)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    EXPECT_EQ(t.AlignTo4k(0), 0u);
    EXPECT_EQ(t.AlignTo4k(15), 8u);
    EXPECT_EQ(t.AlignTo4k(16), 16u);
    EXPECT_EQ(t.AlignStartTo4k(9), 16u);
    EXPECT_EQ(t.AlignStartTo4k(16), 16u);
    EXPECT_TRUE(t.isAlignedTo4k(0x400));
    EXPECT_FALSE(t.isAlignedTo4k(0x401));
}

TEST(Align, SetStartRoundsUpSizeRoundsDown)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    t.SetPartitionStart(5, XBOX_PART6_LBA_START + 3);
    t.SetPartitionSize(5, 0x1003);
    EXPECT_EQ(t.GetPartitionStart(5), (uint64_t)XBOX_PART6_LBA_START + 8);
    EXPECT_EQ(t.GetPartitionSize(5), 0x1000u);
}

TEST(Presets, Stock)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsStock();
    EXPECT_TRUE(t.HasMagic());
    EXPECT_EQ(t.GetActivePartitionCount(), XBOX_STANDARD_PARTITION_COUNT);
    EXPECT_EQ(t.GetPartitionStart(2), (uint64_t)XBOX_SWAPPART1_LBA_START); // X first
    EXPECT_EQ(PartEnd(t, 0), (uint64_t)XBOX_STANDARD_MAX_LBA);              // E last
    EXPECT_EQ(t.GetFirstFreeStart(), (uint64_t)XBOX_PART6_LBA_START);
    ExpectSane(t);

    EXPECT_TRUE(XboxPartitionTableManipulator::IsTableUsable(t.GetPartitionTable(), DISK_1TB));
}

TEST(Presets, FOnlyFillsDisk)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsFOnly();
    ASSERT_TRUE(t.isPartitionEnabled(5));
    EXPECT_EQ(t.GetPartitionStart(5), (uint64_t)XBOX_PART6_LBA_START);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0u);
    EXPECT_LT(LastUsable(t) - PartEnd(t, 5), (uint64_t)LBA_ALIGNMENT);
    ExpectSane(t);
}

TEST(Presets, FOnlyCappedAt16TB)
{
    XboxPartitionTableManipulator t(DISK_40TB, true, false);
    t.SetupPartitionsFOnly();
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_16TB);
    ExpectSane(t);
}

TEST(Presets, FGSplit)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsFGSplit();
    ASSERT_TRUE(t.isPartitionEnabled(5));
    ASSERT_TRUE(t.isPartitionEnabled(6));
    EXPECT_EQ(t.GetPartitionStart(6), PartEnd(t, 5));
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(6), 0u);
    ExpectSane(t);
}

TEST(Presets, FGSplitCappedAt16TB)
{
    XboxPartitionTableManipulator t(DISK_40TB, true, false);
    t.SetupPartitionsFGSplit();
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_16TB);
    EXPECT_EQ(t.GetPartitionSize(6), (uint64_t)LBASIZE_16TB);
    ExpectSane(t);
}

// Just over 32TB: half the disk is over 16TB, but half the space after the
// stock partitions is not, so both can't be 16TB
TEST(Presets, FGSplitJustOver32TB)
{
    const uint64_t disk = 2 * (uint64_t)LBASIZE_16TB + 0x800000;
    XboxPartitionTableManipulator t(disk, true, false);
    t.SetupPartitionsFGSplit();
    ASSERT_TRUE(t.isPartitionEnabled(5));
    ASSERT_TRUE(t.isPartitionEnabled(6));
    EXPECT_LE(t.GetPartitionSize(5), (uint64_t)LBASIZE_16TB);
    EXPECT_LE(t.GetPartitionSize(6), (uint64_t)LBASIZE_16TB);
    ExpectSane(t);
}

TEST(Presets, Stock8GBDriveHasNoRoomForExtras)
{
    XboxPartitionTableManipulator f(XBOX_STANDARD_MAX_LBA, true, false);
    f.SetupPartitionsFOnly();
    EXPECT_FALSE(f.isPartitionEnabled(5));
    ExpectSane(f);

    XboxPartitionTableManipulator fg(XBOX_STANDARD_MAX_LBA, true, false);
    fg.SetupPartitionsFGSplit();
    EXPECT_FALSE(fg.isPartitionEnabled(5));
    EXPECT_FALSE(fg.isPartitionEnabled(6));
    ExpectSane(fg);
}

TEST(FreeSpace, EnablePlacesAtFirstFree)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    t.SetupPartitionsStock();
    t.EnablePartition(5);
    EXPECT_TRUE(t.isPartitionEnabled(5));
    EXPECT_EQ(t.GetPartitionStart(5), (uint64_t)XBOX_PART6_LBA_START);
    EXPECT_EQ(t.GetPartitionSize(5), 0u);
}

TEST(FreeSpace, AfterAndBefore)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartitionBefore(5), 0u); // right after E
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), t.AlignTo4k(LastUsable(t) - PartEnd(t, 5)));

    // G 1M sectors after F
    t.EnablePartition(6);
    t.SetPartitionStart(6, PartEnd(t, 5) + 0x100000);
    t.SetPartitionSize(6, 0x1000);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0x100000u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartitionBefore(6), 0x100000u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(6), t.AlignTo4k(LastUsable(t) - PartEnd(t, 6)));
}

TEST(FreeSpace, DisabledPartitionHasNone)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    const uint64_t start = t.GetPartitionStart(5);
    const uint64_t after = t.GetAvailableFreeSpaceForPartition(5);

    t.DisablePartition(5);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartitionBefore(5), 0u);

    // Enabled again: back in its old range, which is still free
    t.EnablePartition(5);
    EXPECT_EQ(t.GetPartitionStart(5), start);
    EXPECT_EQ(t.GetPartitionSize(5), 0x100000u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), after);
}

TEST(FreeSpace, NoneAroundAnOverlap)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    t.EnablePartition(6);
    t.SetPartitionStart(6, t.GetPartitionStart(5) + 0x80000); // inside F
    t.SetPartitionSize(6, 0x1000);

    EXPECT_EQ(t.GetAvailableFreeSpaceForPartitionBefore(6), 0u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(6), 0u);

    // Moving G back would make the overlap worse: it stays
    const uint64_t g_start = t.GetPartitionStart(6);
    t.PartitionStartBack(6, 0x10000);
    EXPECT_EQ(t.GetPartitionStart(6), g_start);
}

TEST(FreeSpace, NoneWithSharedStart)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    t.EnablePartition(6);
    t.SetPartitionStart(6, t.GetPartitionStart(5));
    t.SetPartitionSize(6, 0x1000);

    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(6), 0u);
    EXPECT_TRUE(HasConflict(t));
}

TEST(FreeSpace, EmptyPartitionAtSameStart)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    const uint64_t start = t.GetPartitionStart(5) + 0x1000;
    t.SetPartitionStart(5, start);
    t.EnablePartition(6);
    t.SetPartitionStart(6, start);
    t.SetPartitionSize(6, 0);

    // The empty one doesn't stop F moving back; F blocks the empty one
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartitionBefore(5), 0x1000u);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartitionBefore(6), 0u);
}

TEST(FreeSpace, FirstFreeStartFindsGap)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    const uint64_t start = t.GetPartitionStart(5);
    t.SetPartitionStart(5, start + 0x100000); // gap before F
    EXPECT_EQ(t.GetFirstFreeStart(), start);

    // No gap: a new partition goes right after F
    t.SetPartitionStart(5, start);
    t.EnablePartition(6);
    EXPECT_EQ(t.GetPartitionStart(6), PartEnd(t, 5));
}

TEST(Editing, GrowAndShrink)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    SetupWithF(t);
    t.PartitionIncreaseSizeBy(5, HUGE_DELTA);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0u); // clamped to the free space
    ExpectSane(t);

    t.PartitionDecreaseSizeBy(5, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionSize(5), 0u);
}

TEST(Editing, StartMovesKeepEnd)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsFOnly();
    const uint64_t end = PartEnd(t, 5);
    const uint64_t start = t.GetPartitionStart(5);

    t.PartitionStartForward(5, GB);
    EXPECT_EQ(t.GetPartitionStart(5), start + GB);
    EXPECT_EQ(PartEnd(t, 5), end);

    t.PartitionStartBack(5, GB);
    EXPECT_EQ(t.GetPartitionStart(5), start);
    EXPECT_EQ(PartEnd(t, 5), end);
}

TEST(Editing, StartForwardKeepsOneBlock)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsFOnly();
    const uint64_t end = PartEnd(t, 5);

    t.PartitionStartForward(5, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBA_ALIGNMENT);
    EXPECT_EQ(PartEnd(t, 5), end);

    t.PartitionStartBack(5, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionStart(5), (uint64_t)XBOX_PART6_LBA_START);
    ExpectSane(t);
}

TEST(Editing, ZeroSizePartitionStartMoves)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsFOnly();
    t.PartitionDecreaseSizeBy(5, HUGE_DELTA);

    t.PartitionStartForward(5, 10 * GB);
    EXPECT_EQ(t.GetPartitionStart(5), XBOX_PART6_LBA_START + 10 * GB);
    EXPECT_EQ(t.GetPartitionSize(5), 0u);

    t.PartitionStartBack(5, GB);
    EXPECT_EQ(t.GetPartitionStart(5), XBOX_PART6_LBA_START + 9 * GB);
    EXPECT_EQ(t.GetPartitionSize(5), 0u);

    t.MovePartitionStartTo(5, XBOX_PART6_LBA_START);
    EXPECT_EQ(t.GetPartitionStart(5), (uint64_t)XBOX_PART6_LBA_START);
}

TEST(Editing, TypedValues)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsFOnly();

    t.ResizePartitionTo(5, 100 * GB);
    EXPECT_EQ(t.GetPartitionSize(5), 100 * GB);
    t.ResizePartitionTo(5, 100 * GB + 3);
    EXPECT_EQ(t.GetPartitionSize(5), 100 * GB); // rounds down
    t.ResizePartitionTo(5, HUGE_DELTA);
    EXPECT_EQ(t.GetAvailableFreeSpaceForPartition(5), 0u); // clamped
    ExpectSane(t);

    t.ResizePartitionTo(5, 50 * GB);
    const uint64_t end = PartEnd(t, 5);
    t.MovePartitionStartTo(5, XBOX_PART6_LBA_START + GB + 3);
    EXPECT_EQ(t.GetPartitionStart(5), XBOX_PART6_LBA_START + GB + 8); // aligned up
    EXPECT_EQ(PartEnd(t, 5), end);
    t.MovePartitionStartTo(5, XBOX_PART6_LBA_START);
    EXPECT_EQ(t.GetPartitionStart(5), (uint64_t)XBOX_PART6_LBA_START);
    EXPECT_EQ(PartEnd(t, 5), end);
}

// A table from another tool: an empty partition at an unaligned start, 3
// sectors after the previous one. Moving it back must not round it forward.
TEST(Editing, UnalignedStartNeverMovesForward)
{
    XboxPartitionTableManipulator split(DISK_1TB, true, false);
    split.SetupPartitionsFGSplit();
    XboxPartitionTable table = split.GetPartitionTable();
    table.TableEntries[6].LBASize -= 8;
    table.TableEntries[7] = table.TableEntries[6];
    table.TableEntries[7].LBAStart = table.TableEntries[6].LBAStart + table.TableEntries[6].LBASize + 3;
    table.TableEntries[7].LBASize = 0;

    XboxPartitionTableManipulator t(table, DISK_1TB, true, false);
    const uint64_t start = t.GetPartitionStart(7);
    ASSERT_FALSE(IsAligned(start));

    t.PartitionStartBack(7, 1);
    EXPECT_LE(t.GetPartitionStart(7), start);
    t.PartitionStartBack(7, 8);
    EXPECT_LE(t.GetPartitionStart(7), start);
    EXPECT_GE(t.GetPartitionStart(7), PartEnd(t, 6));
    EXPECT_FALSE(HasConflict(t));
}

// Table version: 0x00 v3, 0x01 v4, 0x03 Titan

namespace
{
XboxPartitionTable StockTableWithVersion(unsigned char version)
{
    XboxPartitionTableManipulator stock(DISK_128GB, true, false);
    XboxPartitionTable table = stock.GetPartitionTable();
    table.Version = version;
    return table;
}
} // namespace

TEST(TableVersion, StockPresetMatchesMode)
{
    XboxPartitionTableManipulator v4(DISK_1TB, true, false);
    EXPECT_EQ(v4.GetPartitionTable().Version, XBOX_TABLE_VERSION_LBA48_V4);

    XboxPartitionTableManipulator v3(DISK_1TB, false, false);
    EXPECT_EQ(v3.GetPartitionTable().Version, XBOX_TABLE_VERSION_LBA48_V3);

    // The rest of the header stays zero (the old "magic bit" was the version)
    const XboxPartitionTable table = v4.GetPartitionTable();
    for (uint32_t i = 0; i < XboxPartitionTableHeaderReservedSize; i++)
    {
        EXPECT_EQ(table.Reserved[i], 0) << "Reserved[" << i << "]";
    }

    EXPECT_EQ(XboxPartitionTableManipulator::VersionFor(true), XBOX_TABLE_VERSION_LBA48_V4);
    EXPECT_EQ(XboxPartitionTableManipulator::VersionFor(false), XBOX_TABLE_VERSION_LBA48_V3);
}

TEST(TableVersion, KnownVersions)
{
    for (unsigned char v : {XBOX_TABLE_VERSION_LBA48_V3, XBOX_TABLE_VERSION_LBA48_V4, XBOX_TABLE_VERSION_TITAN})
    {
        const XboxPartitionTable table = StockTableWithVersion(v);
        EXPECT_FALSE(XboxPartitionTableManipulator::HasUnknownVersion(table)) << (int)v;
        EXPECT_TRUE(XboxPartitionTableManipulator::IsTableUsable(table, DISK_128GB)) << (int)v;
    }
}

TEST(TableVersion, UnknownVersionIsUsableButFlagged)
{
    for (unsigned char v : {(unsigned char)0x02, (unsigned char)0x04, (unsigned char)0xFF})
    {
        const XboxPartitionTable table = StockTableWithVersion(v);
        EXPECT_TRUE(XboxPartitionTableManipulator::HasUnknownVersion(table)) << (int)v;
        EXPECT_TRUE(XboxPartitionTableManipulator::IsTableUsable(table, DISK_128GB)) << (int)v;
    }
}

TEST(TableVersion, NoMagicNoVersion)
{
    XboxPartitionTable table = StockTableWithVersion(0x02);
    table.Magic[0] = 0;
    EXPECT_FALSE(XboxPartitionTableManipulator::HasUnknownVersion(table));
}

// LBA48 v3 (also force_lba48_v3): partitions at most 1TB, 64 KB clusters

namespace
{
const uint64_t DISK_1_5TB = 3 * 1024ULL * 1024 * 1024; // sectors, under 2TB so no high LBA bits
const uint64_t DISK_4TB = 8 * 1024ULL * 1024 * 1024;
} // namespace

TEST(Lba48V3, Limits)
{
    XboxPartitionTableManipulator v3(DISK_1TB, false, false);
    EXPECT_EQ(v3.GetMaxPartitionSize(), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_EQ(v3.GetMaxClusterSize(), 64u);

    XboxPartitionTableManipulator v4(DISK_1TB, true, false);
    EXPECT_EQ(v4.GetMaxClusterSize(), 0u); // no limit

    // 1TB is the largest size that still gets 64 KB clusters
    EXPECT_EQ(v3.CalculateClusterSize(LBASIZE_LBA48_V3_MAX), 64u);
    EXPECT_EQ(v3.CalculateClusterSize(LBASIZE_LBA48_V3_MAX + LBA_ALIGNMENT), 128u);
}

TEST(Lba48V3, PresetsStopAt1TB)
{
    // v3 only addresses 32 bit LBAs, so a 4TB drive is used as 2TB
    XboxPartitionTableManipulator f(DISK_4TB, false, false);
    f.SetupPartitionsFOnly();
    EXPECT_EQ(f.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_LE(f.CalculateClusterSizeForPartition(5), 64u);
    ExpectSane(f);

    XboxPartitionTableManipulator fg(DISK_4TB, false, false);
    fg.SetupPartitionsFGSplit();
    EXPECT_LE(fg.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_LE(fg.GetPartitionSize(6), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_LE(fg.CalculateClusterSizeForPartition(5), 64u);
    EXPECT_LE(fg.CalculateClusterSizeForPartition(6), 64u);
    ExpectSane(fg);
}

TEST(Lba48V3, GrowingStopsAt1TB)
{
    XboxPartitionTableManipulator t(DISK_1_5TB, false, false);
    t.SetupPartitionsFOnly();
    ASSERT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_GT(t.GetAvailableFreeSpaceForPartition(5), 0u); // room on the disk...
    EXPECT_EQ(t.GetGrowthLimit(5), 0u);                    // ...but not for F

    t.PartitionIncreaseSizeBy(5, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    t.ResizePartitionTo(5, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);

    // Moving the start back grows it too: also no further than 1TB
    const uint64_t end = PartEnd(t, 5);
    t.PartitionStartForward(5, 10 * GB);
    t.PartitionStartBack(5, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_EQ(PartEnd(t, 5), end);
    ExpectSane(t);
}

TEST(Lba48V3, OversizedPartitionCanOnlyShrink)
{
    // Made with v4 (F over 1TB), then used with v3
    XboxPartitionTableManipulator v4(DISK_1_5TB, true, false);
    v4.SetupPartitionsFOnly();
    ASSERT_GT(v4.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);

    XboxPartitionTableManipulator t(v4.GetPartitionTable(), DISK_1_5TB, false, false);
    const uint64_t size = t.GetPartitionSize(5);
    EXPECT_EQ(t.GetGrowthLimit(5), 0u);
    EXPECT_EQ(t.GetGrowthLimitBefore(5), 0u);
    EXPECT_GT(t.CalculateClusterSizeForPartition(5), 64u); // would need 128 KB

    t.PartitionIncreaseSizeBy(5, GB);
    EXPECT_EQ(t.GetPartitionSize(5), size);

    t.ResizePartitionTo(5, LBASIZE_LBA48_V3_MAX);
    EXPECT_EQ(t.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_LE(t.CalculateClusterSizeForPartition(5), 64u);
}

TEST(Lba48V3, V4HasNoSuchLimit)
{
    XboxPartitionTableManipulator t(DISK_4TB, true, false);
    t.SetupPartitionsFOnly();
    EXPECT_GT(t.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
    EXPECT_EQ(t.GetGrowthLimit(5), t.GetAvailableFreeSpaceForPartition(5));
}

// Nothing may start before XBOX_SWAPPART1_LBA_START (0x400)

namespace
{

// Stock table plus partition 8 at the given start and size, as a damaged or
// foreign kernel table could have it
XboxPartitionTable TableWithPartition8(uint64_t start, uint64_t size, bool enabled)
{
    XboxPartitionTableManipulator stock(DISK_128GB, true, false);
    XboxPartitionTable table = stock.GetPartitionTable();
    XboxPartitionTableEntry &entry = table.TableEntries[7];
    entry.LBAStart = (uint32_t)start;
    entry.LBAStart_high = 0;
    entry.LBASize = (uint32_t)size;
    entry.LBASize_high = 0;
    entry.Flags = enabled ? PE_PARTFLAGS_IN_USE : PE_PARTFLAGS_EMPTY;
    return table;
}

} // namespace

TEST(ReservedArea, PresetsAreClear)
{
    int part = -1;
    XboxPartitionTableManipulator t(DISK_1TB, true, false);
    t.SetupPartitionsStock();
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));
    t.SetupPartitionsFOnly();
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));
    t.SetupPartitionsFGSplit();
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));
}

TEST(ReservedArea, FindsPartitionStartingBefore0x400)
{
    XboxPartitionTableManipulator t(TableWithPartition8(0x200, 0x1000, true), DISK_128GB, true, false);
    int part = -1;
    EXPECT_TRUE(t.FindPartitionBeforeUsableStart(&part));
    EXPECT_EQ(part, 7);

    // Starting exactly at 0x400 is fine (X does)
    XboxPartitionTableManipulator stock(DISK_128GB, true, false);
    EXPECT_EQ(stock.GetPartitionStart(2), (uint64_t)XBOX_SWAPPART1_LBA_START);
    EXPECT_FALSE(stock.FindPartitionBeforeUsableStart(&part));
}

TEST(ReservedArea, DisabledEntriesDontCount)
{
    XboxPartitionTableManipulator t(TableWithPartition8(0x200, 0x1000, false), DISK_128GB, true, false);
    int part = -1;
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));

    // Enabling it places it at the first free spot instead of its old range
    t.EnablePartition(7);
    ASSERT_TRUE(t.isPartitionEnabled(7));
    EXPECT_GE(t.GetPartitionStart(7), (uint64_t)XBOX_SWAPPART1_LBA_START);
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));
}

TEST(ReservedArea, EditorNeverMovesBelow0x400)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    t.SetupPartitionsStock();

    // X is first, at 0x400: its start can't go back, typed or stepped
    t.PartitionStartBack(2, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionStart(2), (uint64_t)XBOX_SWAPPART1_LBA_START);
    t.MovePartitionStartTo(2, 0x100);
    EXPECT_EQ(t.GetPartitionStart(2), (uint64_t)XBOX_SWAPPART1_LBA_START);

    // With X gone, Y's start goes back as far as 0x400 and no further
    t.DisablePartition(2);
    t.PartitionStartBack(3, HUGE_DELTA);
    EXPECT_EQ(t.GetPartitionStart(3), (uint64_t)XBOX_SWAPPART1_LBA_START);

    // An empty partition moves instead, also stopping at 0x400
    t.DisablePartition(3);
    t.EnablePartition(5);
    t.MovePartitionStartTo(5, 0x100);
    EXPECT_GE(t.GetPartitionStart(5), (uint64_t)XBOX_SWAPPART1_LBA_START);

    int part = -1;
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));
}

TEST(ReservedArea, ResetRefusesTheReservedArea)
{
    XboxPartitionTableManipulator t(DISK_128GB, true, false);
    const XboxPartitionTable bad = TableWithPartition8(0x200, 0x100, true);
    EXPECT_FALSE(t.RestorePartitionEntry(7, bad.TableEntries[7]));
    EXPECT_FALSE(t.isPartitionEnabled(7));
}

TEST(ReservedArea, StartForwardFixesIt)
{
    XboxPartitionTableManipulator t(TableWithPartition8(0x200, 0x1000, true), DISK_128GB, true, false);
    t.PartitionStartForward(7, 0x200);
    EXPECT_EQ(t.GetPartitionStart(7), (uint64_t)XBOX_SWAPPART1_LBA_START);
    EXPECT_EQ(PartEnd(t, 7), 0x1200u); // the end stays
    int part = -1;
    EXPECT_FALSE(t.FindPartitionBeforeUsableStart(&part));
}

TEST(ReservedArea, TableChecksRejectIt)
{
    EXPECT_FALSE(XboxPartitionTableManipulator::IsTableUsable(TableWithPartition8(0x200, 0x100, true), DISK_128GB));
}

TEST(Conflicts, EmptyPartitionAtSameStart)
{
    XboxPartitionTableManipulator split(DISK_1TB, true, false);
    split.SetupPartitionsFGSplit();
    EXPECT_FALSE(HasConflict(split));

    XboxPartitionTable table = split.GetPartitionTable();
    table.TableEntries[7] = table.TableEntries[6];
    table.TableEntries[7].LBASize = 0;
    table.TableEntries[7].LBASize_high = 0;
    XboxPartitionTableManipulator t(table, DISK_1TB, true, false);

    int a = -1, b = -1;
    EXPECT_TRUE(t.FindConflict(&a, &b));
    EXPECT_EQ(a, 6);
    EXPECT_EQ(b, 7);
}

TEST(Conflicts, Overlap)
{
    XboxPartitionTableManipulator split(DISK_1TB, true, false);
    split.SetupPartitionsFGSplit();
    XboxPartitionTable table = split.GetPartitionTable();
    table.TableEntries[6].LBAStart -= 8; // G starts 8 sectors inside F

    XboxPartitionTableManipulator t(table, DISK_1TB, true, false);
    int a = -1, b = -1;
    EXPECT_TRUE(t.FindConflict(&a, &b));
    EXPECT_EQ(a, 5);
    EXPECT_EQ(b, 6);
    EXPECT_FALSE(XboxPartitionTableManipulator::IsTableUsable(table, DISK_1TB));
}

TEST(TableChecks, IsTableUsable)
{
    XboxPartitionTableManipulator stock(DISK_1TB, true, false);
    const XboxPartitionTable table = stock.GetPartitionTable();
    EXPECT_TRUE(XboxPartitionTableManipulator::IsTableUsable(table, DISK_1TB));

    XboxPartitionTable no_magic = table;
    no_magic.Magic[0] = 0;
    EXPECT_FALSE(XboxPartitionTableManipulator::IsTableUsable(no_magic, DISK_1TB));

    XboxPartitionTable empty_part = table;
    empty_part.TableEntries[0].LBASize = 0;
    EXPECT_FALSE(XboxPartitionTableManipulator::IsTableUsable(empty_part, DISK_1TB));

    EXPECT_FALSE(XboxPartitionTableManipulator::IsTableUsable(table, XBOX_STANDARD_MAX_LBA - 1)); // past the end

    XboxPartitionTable none_in_use = table;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        none_in_use.TableEntries[i].Flags = PE_PARTFLAGS_EMPTY;
    }
    EXPECT_FALSE(XboxPartitionTableManipulator::IsTableUsable(none_in_use, DISK_1TB));
}

// F from the first free sector to end_lba, on a disk of disk_lba sectors
XboxPartitionTableManipulator WithFTo(uint64_t end_lba, uint64_t disk_lba, bool physical_4k)
{
    XboxPartitionTableManipulator t(disk_lba, true, physical_4k);
    t.SetupPartitionsStock();
    t.EnablePartition(5);
    XboxPartitionTable table = t.GetPartitionTable();
    const uint64_t size = end_lba - XBOX_PART6_LBA_START;
    table.TableEntries[5].LBAStart = XBOX_PART6_LBA_START;
    table.TableEntries[5].LBASize = (uint32_t)size;
    table.TableEntries[5].LBASize_high = (uint16_t)(size >> 32);
    return XboxPartitionTableManipulator(table, disk_lba, true, physical_4k);
}

TEST(BackupShrink, LastSectorOn512nDrive)
{
    XboxPartitionTableManipulator t = WithFTo(DISK_1TB, DISK_1TB, false);
    ASSERT_TRUE(t.OverlapsBackupTable());
    ASSERT_EQ(t.FindBackupShrinkCandidate(), 5);

    t.PartitionDecreaseSizeBy(5, LBA_ALIGNMENT);
    EXPECT_FALSE(t.OverlapsBackupTable());
    EXPECT_EQ(PartEnd(t, 5), DISK_1TB - LBA_ALIGNMENT);
    EXPECT_EQ(t.FindBackupShrinkCandidate(), -1);
}

TEST(BackupShrink, Last4kBlockOn512eDrive)
{
    // The disk ends mid-block, so the backup is in the last whole 4K block
    const uint64_t disk = DISK_1TB + 5;
    XboxPartitionTableManipulator t = WithFTo(DISK_1TB, disk, true);
    ASSERT_EQ(t.GetBackupTableLba(), DISK_1TB - LBA_ALIGNMENT);
    ASSERT_EQ(t.FindBackupShrinkCandidate(), 5);

    t.PartitionDecreaseSizeBy(5, LBA_ALIGNMENT);
    EXPECT_FALSE(t.OverlapsBackupTable());
}

TEST(BackupShrink, NothingWithoutOverlap)
{
    XboxPartitionTableManipulator t(DISK_1TB, true, true);
    t.SetupPartitionsFOnly();
    EXPECT_FALSE(t.OverlapsBackupTable());
    EXPECT_EQ(t.FindBackupShrinkCandidate(), -1);
}

TEST(BackupShrink, NotWhenOneBlockIsntEnough)
{
    // F runs 4K past the disk end, so dropping one block isn't enough
    XboxPartitionTableManipulator t = WithFTo(DISK_1TB + LBA_ALIGNMENT, DISK_1TB, false);
    ASSERT_TRUE(t.OverlapsBackupTable());
    EXPECT_EQ(t.FindBackupShrinkCandidate(), -1);
}

TEST(BackupShrink, NotWithUnalignedSize)
{
    XboxPartitionTableManipulator t = WithFTo(DISK_1TB + 3, DISK_1TB + 3, false);
    ASSERT_TRUE(t.OverlapsBackupTable());
    EXPECT_EQ(t.FindBackupShrinkCandidate(), -1);
}

TEST(Fatx, Layout)
{
    // 512 MB, 16 KB clusters: 0x8001 entries of 2 bytes, page rounded
    const FatxLayout layout = FatxLayoutFor(0x100000, 0x4000);
    EXPECT_EQ(layout.bytes_per_entry, 2u);
    EXPECT_EQ(layout.bytes_per_fat, 0x11000u);
    EXPECT_EQ(layout.data_offset, 0x12000u);
    EXPECT_EQ(layout.cluster_limit, 32764u);

    // 4 byte entries from 0xFFF0 on (with the reserved one)
    EXPECT_EQ(FatxLayoutFor(0xFFEEULL * 0x20, 0x4000).bytes_per_entry, 2u);
    EXPECT_EQ(FatxLayoutFor(0xFFEFULL * 0x20, 0x4000).bytes_per_entry, 4u);
}

TEST(Fatx, ClusterAt)
{
    const FatxLayout layout = FatxLayoutFor(0x100000, 0x4000);
    EXPECT_EQ(FatxClusterAt(layout, 0x4000, 0), 0u);                         // header
    EXPECT_EQ(FatxClusterAt(layout, 0x4000, layout.data_offset - 1), 0u);    // FAT
    EXPECT_EQ(FatxClusterAt(layout, 0x4000, layout.data_offset), 1u);
    EXPECT_EQ(FatxClusterAt(layout, 0x4000, layout.data_offset + 0x4000), 2u);
    // The last 4 KB are after the last whole cluster
    EXPECT_EQ(FatxClusterAt(layout, 0x4000, 0x20000000 - 0x1000), 0u);
}

TEST(Fatx, ShrinkingBy4kMayMoveTheData)
{
    // One cluster fewer crosses a FAT page boundary, which would move all data
    const FatxLayout before = FatxLayoutFor(0x100000, 0x4000);
    const FatxLayout after = FatxLayoutFor(0x100000 - LBA_ALIGNMENT, 0x4000);
    EXPECT_NE(before.data_offset, after.data_offset);

    // Usually it doesn't
    const FatxLayout before2 = FatxLayoutFor(0x100040, 0x4000);
    const FatxLayout after2 = FatxLayoutFor(0x100040 - LBA_ALIGNMENT, 0x4000);
    EXPECT_EQ(before2.bytes_per_fat, after2.bytes_per_fat);
    EXPECT_EQ(before2.bytes_per_entry, after2.bytes_per_entry);
}

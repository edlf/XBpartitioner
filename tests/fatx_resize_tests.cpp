// Tests for resizing with moves. Every file must read the same afterwards,
// including when the power is cut at any write and the resize is resumed.

#include <gtest/gtest.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "fatx_check.hpp"
#include "fatx_resize.hpp"
#include "fatx_writer.hpp"
#include "test_images.hpp"

namespace
{

// Files near the start, where a growing FAT will go, and a folder of many
// small files
void FillStart(FatxWriter &writer)
{
    AddFile(writer, writer.RootCluster(), "first.bin", CLUSTER * 3 + 17, 1);
    uint32_t saves;
    ASSERT_TRUE(writer.MakeDirectory(writer.RootCluster(), "saves", &saves));
    for (int i = 0; i < 300; i++) // Two folder clusters' worth of entries
    {
        char name[16];
        sprintf_s(name, "s%03d.dat", i);
        AddFile(writer, saves, name, (i % 3) * 700 + 1, 100 + i);
    }
    uint32_t sub;
    ASSERT_TRUE(writer.MakeDirectory(saves, "sub", &sub));
    AddFile(writer, sub, "big.bin", CLUSTER * 20 + 5, 7);
    AddFile(writer, writer.RootCluster(), "empty", 0, 0);
}

// Files at the end, which a shrink cuts off, and a file whose chain goes in
// and out of that area
void FillEnd(FatxWriter &writer, uint32_t end_start)
{
    writer.SetNextFree(end_start - 3);
    AddFile(writer, writer.RootCluster(), "across.bin", CLUSTER * 8, 21); // Starts before end_start
    uint32_t games;
    ASSERT_TRUE(writer.MakeDirectory(writer.RootCluster(), "games", &games));
    AddFile(writer, games, "default.xbe", CLUSTER * 5 + 100, 22);
    uint32_t deep;
    ASSERT_TRUE(writer.MakeDirectory(games, "deep", &deep));
    AddFile(writer, deep, "data.bin", CLUSTER * 2, 23);

    // Build a chain that goes out, in, then out again
    writer.SetNextFree(end_start + 200);
    uint32_t p, q, r;
    AddFile(writer, writer.RootCluster(), "mixed.bin", CLUSTER * 2, 24, &p);
    writer.SetNextFree(10);
    AddFile(writer, writer.RootCluster(), "q", 1, 25, &q);
    writer.SetNextFree(end_start + 300);
    AddFile(writer, writer.RootCluster(), "r", 1, 26, &r);
    writer.SetFat(writer.GetFat(p), q);
    writer.SetFat(q, r);

    // mixed.bin now owns all four clusters; delete q and r
    for (uint32_t i = 0;; i++)
    {
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), i, &entry, &offset));
        const std::string name(entry.name, entry.name_length < 43 ? entry.name_length : 0);
        if (name == "mixed.bin")
        {
            entry.file_size = CLUSTER * 3 + 1;
            ASSERT_TRUE(writer.SetEntry(offset, entry));
        }
        else if (name == "q" || name == "r")
        {
            entry.name_length = FATX_DIRENT_DELETED;
            ASSERT_TRUE(writer.SetEntry(offset, entry));
            if (name == "r")
            {
                break;
            }
        }
    }
}

struct Case
{
    SparseDevice device;
    uint64_t old_lba;
    uint64_t requested_lba;
};

// 64 MB to 352 MB: the FAT grows by two clusters, so the root folder and
// first.bin have to move
void MakeGrow(Case &c)
{
    c.old_lba = 64 * MB / HDD_SECTOR_SIZE;
    c.requested_lba = 352 * MB / HDD_SECTOR_SIZE;
    FatxWriter writer(c.device, c.old_lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    FillStart(writer);
    ASSERT_TRUE(writer.Flush());
}

// 400 MB to 150 MB: the FAT shrinks by two clusters, and everything from
// cluster 9597 on has to move down
const uint32_t SHRINK_CUT = 9597;

void MakeShrink(Case &c)
{
    c.old_lba = 400 * MB / HDD_SECTOR_SIZE;
    c.requested_lba = 150 * MB / HDD_SECTOR_SIZE;
    FatxWriter writer(c.device, c.old_lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    FillStart(writer);
    FillEnd(writer, SHRINK_CUT);
    ASSERT_TRUE(writer.Flush());
}

} // namespace

TEST(FatxMoveResize, Target)
{
    // 64 MB with 16 KB clusters has a 12 KB FAT. Growing it in whole clusters
    // gives 28 KB (up to 224 MB), then 44 KB (up to 352 MB).
    const uint64_t old_lba = 64 * MB / 512;
    EXPECT_EQ(FatxMoveResizeTarget(old_lba, CLUSTER, 352 * MB / 512), 352 * MB / 512 - 8);
    const uint64_t at_300 = FatxMoveResizeTarget(old_lba, CLUSTER, 300 * MB / 512);
    EXPECT_EQ(FatxLayoutFor(at_300, CLUSTER).bytes_per_fat, 28u * 1024);
    EXPECT_LE(at_300, 300 * MB / 512);

    // 400 MB has a 52 KB FAT. At 150 MB it's 20 KB, two clusters less. At
    // 100 MB it would be 16 KB, which isn't, so it rounds down to a 4 KB FAT.
    const uint64_t big = 400 * MB / 512;
    EXPECT_EQ(FatxMoveResizeTarget(big, CLUSTER, 150 * MB / 512), 150 * MB / 512);
    const uint64_t to_100 = FatxMoveResizeTarget(big, CLUSTER, 100 * MB / 512);
    EXPECT_EQ(FatxLayoutFor(to_100, CLUSTER).bytes_per_fat, 4096u);
    EXPECT_LE(to_100, 32 * MB / 512);
}

TEST(FatxMoveResize, GrowKeepsEveryFile)
{
    Case c;
    MakeGrow(c);
    const auto before = Contents(c.device, c.old_lba);
    SizeRecord table(c.old_lba);
    FatxMoveResult *result = new FatxMoveResult;
    ASSERT_TRUE(FatxResizeWithMoves(c.device, c.old_lba, c.requested_lba, table, true, result)) << result->status;
    EXPECT_EQ(result->status, FATX_MOVE_DONE);
    EXPECT_EQ(result->shift, 2);
    EXPECT_GT(result->clusters_moved, 0u);
    EXPECT_EQ(table.size, result->new_lba_size);

    const FatxCheckResult check = Check(c.device, table.size);
    EXPECT_EQ(check.status, FATX_CHECK_DONE);
    EXPECT_EQ(check.errors, 0u);
    EXPECT_EQ(check.warnings, 0u);
    ExpectSameFiles(before, Contents(c.device, table.size));
    delete result;
}

TEST(FatxMoveResize, ShrinkKeepsEveryFile)
{
    Case c;
    MakeShrink(c);
    const auto before = Contents(c.device, c.old_lba);
    ASSERT_EQ(Check(c.device, c.old_lba).errors, 0u);

    SizeRecord table(c.old_lba);
    FatxMoveResult *result = new FatxMoveResult;
    ASSERT_TRUE(FatxResizeWithMoves(c.device, c.old_lba, c.requested_lba, table, true, result)) << result->status;
    EXPECT_EQ(result->shift, -2);
    EXPECT_GT(result->clusters_moved, 10u);
    EXPECT_EQ(table.size, result->new_lba_size);

    const FatxCheckResult check = Check(c.device, table.size);
    EXPECT_EQ(check.errors, 0u);
    EXPECT_EQ(check.warnings, 0u);
    ExpectSameFiles(before, Contents(c.device, table.size));
    delete result;
}

TEST(FatxMoveResize, DryRunWritesNothing)
{
    Case c;
    MakeShrink(c);
    PowerCutDevice no_writes(c.device, 0);
    SizeRecord table(c.old_lba);
    FatxMoveResult *result = new FatxMoveResult;
    EXPECT_TRUE(FatxResizeWithMoves(no_writes, c.old_lba, c.requested_lba, table, false, result));
    EXPECT_EQ(result->status, FATX_MOVE_PLANNED);
    EXPECT_GT(result->clusters_to_move, 10u);
    EXPECT_EQ(no_writes.Writes(), 0u);
    EXPECT_EQ(table.calls, 0);
    delete result;
}

TEST(FatxMoveResize, Refusals)
{
    SizeRecord table(0);
    FatxMoveResult *result = new FatxMoveResult;

    // Errors: a file longer than its chain
    {
        Case c;
        MakeGrow(c);
        FatxWriter writer(c.device, c.old_lba * HDD_SECTOR_SIZE);
        ASSERT_TRUE(writer.Load());
        FatxDirent entry;
        uint64_t offset;
        ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), 0, &entry, &offset));
        entry.file_size *= 10;
        ASSERT_TRUE(writer.SetEntry(offset, entry));
        EXPECT_FALSE(FatxResizeWithMoves(c.device, c.old_lba, c.requested_lba, table, true, result));
        EXPECT_EQ(result->status, FATX_MOVE_HAS_ERRORS);
    }

    // Same FAT size: that's the in-place resize
    {
        Case c;
        MakeGrow(c);
        EXPECT_FALSE(FatxResizeWithMoves(c.device, c.old_lba, c.old_lba + 8, table, true, result));
        EXPECT_EQ(result->status, FATX_MOVE_SAME_FAT);
    }

    // 16 KB clusters past 256 GB (only the header is read)
    {
        Case c;
        MakeGrow(c);
        EXPECT_FALSE(FatxResizeWithMoves(c.device, 0x1F000000, 0x30000000, table, false, result));
        EXPECT_EQ(result->status, FATX_MOVE_CLUSTER_SIZE);
    }

    // Shrinking from 4 byte entries to where 2 would do
    {
        Case c;
        c.old_lba = 1200 * MB / 512;
        FatxWriter writer(c.device, c.old_lba * HDD_SECTOR_SIZE, CLUSTER);
        ASSERT_TRUE(writer.Format());
        ASSERT_EQ(writer.Layout().bytes_per_entry, 4u);
        EXPECT_FALSE(FatxResizeWithMoves(c.device, c.old_lba, 500 * MB / 512, table, false, result));
        EXPECT_EQ(result->status, FATX_MOVE_ENTRY_SIZE);
    }

    // Not FATX
    {
        SparseDevice blank;
        EXPECT_FALSE(FatxResizeWithMoves(blank, 64 * MB / 512, 352 * MB / 512, table, false, result));
        EXPECT_EQ(result->status, FATX_MOVE_NOT_FATX);
    }
    delete result;
}

TEST(FatxMoveResize, NoRoom)
{
    // A nearly full 400 MB partition can't shrink: the data won't fit. The
    // big file's chain is built by hand, without writing its contents.
    Case c;
    c.old_lba = 400 * MB / HDD_SECTOR_SIZE;
    FatxWriter writer(c.device, c.old_lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    const uint32_t clusters = 24000;
    uint32_t first;
    AddFile(writer, writer.RootCluster(), "huge.bin", 1, 3, &first);
    for (uint32_t i = 0; i + 1 < clusters; i++)
    {
        writer.SetFat(first + i, first + i + 1);
    }
    writer.SetFat(first + clusters - 1, FATX_FAT_END);
    FatxDirent entry;
    uint64_t offset;
    ASSERT_TRUE(writer.GetEntry(writer.RootCluster(), 0, &entry, &offset));
    entry.file_size = clusters * CLUSTER;
    ASSERT_TRUE(writer.SetEntry(offset, entry));
    ASSERT_TRUE(writer.Flush());
    ASSERT_EQ(Check(c.device, c.old_lba).errors, 0u);

    SizeRecord table(c.old_lba);
    FatxMoveResult *result = new FatxMoveResult;
    EXPECT_FALSE(FatxResizeWithMoves(c.device, c.old_lba, 150 * MB / HDD_SECTOR_SIZE, table, true, result));
    EXPECT_EQ(result->status, FATX_MOVE_NO_ROOM);
    EXPECT_EQ(table.calls, 0);
    delete result;
}

TEST(FatxMoveResize, TableWriteFailureCanBeFinished)
{
    Case c;
    MakeGrow(c);
    SizeRecord table(c.old_lba);
    table.fail = true;
    FatxMoveResult *result = new FatxMoveResult;

    // Everything on disk is done, only the table is left
    EXPECT_FALSE(FatxResizeWithMoves(c.device, c.old_lba, c.requested_lba, table, true, result));
    EXPECT_EQ(result->status, FATX_MOVE_INTERRUPTED);
    EXPECT_TRUE(result->table_failed);
    EXPECT_EQ(table.size, c.old_lba);
    EXPECT_EQ(Check(c.device, c.old_lba).status, FATX_CHECK_RESIZING);

    // Failing again keeps it resumable
    EXPECT_FALSE(FatxResumeResize(c.device, table, result));
    EXPECT_TRUE(result->table_failed);

    table.fail = false;
    ASSERT_TRUE(FatxResumeResize(c.device, table, result));
    EXPECT_FALSE(result->table_failed);
    EXPECT_NE(table.size, c.old_lba);
    EXPECT_EQ(Check(c.device, table.size).errors, 0u);
    delete result;
}

TEST(FatxMoveResize, InterruptedShowsInTheCheck)
{
    Case c;
    MakeGrow(c);
    SizeRecord table(c.old_lba);
    FatxMoveResult *result = new FatxMoveResult;

    // Count the writes, so we can cut during the renumbering
    SparseDevice copy = c.device;
    PowerCutDevice counter(copy, ~0ULL);
    SizeRecord copy_table(c.old_lba);
    ASSERT_TRUE(FatxResizeWithMoves(counter, c.old_lba, c.requested_lba, copy_table, true, result));
    const uint64_t total = counter.Writes();

    PowerCutDevice late(c.device, total - 5);
    EXPECT_FALSE(FatxResizeWithMoves(late, c.old_lba, c.requested_lba, table, true, result));
    EXPECT_EQ(result->status, FATX_MOVE_INTERRUPTED);
    EXPECT_EQ(Check(c.device, c.old_lba).status, FATX_CHECK_RESIZING);

    uint64_t old_size, new_size;
    ASSERT_TRUE(FatxFindInterruptedResize(c.device, &old_size, &new_size));
    EXPECT_EQ(old_size, c.old_lba);

    // Resizing refuses until it's finished
    EXPECT_FALSE(FatxResizeWithMoves(c.device, c.old_lba, c.requested_lba, table, false, result));
    EXPECT_EQ(result->status, FATX_MOVE_INTERRUPTED);

    ASSERT_TRUE(FatxResumeResize(c.device, table, result));
    EXPECT_EQ(Check(c.device, table.size).errors, 0u);
    EXPECT_FALSE(FatxFindInterruptedResize(c.device, &old_size, &new_size));

    // Nothing left to resume
    EXPECT_FALSE(FatxResumeResize(c.device, table, result));
    EXPECT_EQ(result->status, FATX_MOVE_NOTHING_TO_RESUME);
    delete result;
}

namespace
{

// Cuts the power at every write in turn. Afterwards the partition must be
// either the old one (lost clusters at worst) or an unfinished resize that
// resumes cleanly, with every file intact.
void PowerCutEverywhere(void (*make)(Case &), uint64_t step)
{
    Case base;
    make(base);
    const auto before = Contents(base.device, base.old_lba);

    uint64_t total;
    uint64_t new_lba;
    {
        SparseDevice copy = base.device;
        PowerCutDevice counter(copy, ~0ULL);
        SizeRecord table(base.old_lba);
        FatxMoveResult result;
        ASSERT_TRUE(FatxResizeWithMoves(counter, base.old_lba, base.requested_lba, table, true, &result));
        total = counter.Writes();
        new_lba = result.new_lba_size;
    }
    ASSERT_GT(total, 20u);

    int resumed = 0;
    for (uint64_t cut = 0; cut < total; cut += step)
    {
        SCOPED_TRACE("power cut at write " + std::to_string(cut) + " of " + std::to_string(total));
        SparseDevice disk = base.device;
        SizeRecord table(base.old_lba);
        FatxMoveResult result;
        {
            PowerCutDevice cut_device(disk, cut);
            EXPECT_FALSE(FatxResizeWithMoves(cut_device, base.old_lba, base.requested_lba, table, true, &result));
        }

        uint64_t old_size, new_size;
        if (FatxFindInterruptedResize(disk, &old_size, &new_size))
        {
            // Half the time, cut the power again during the resume
            if (cut % 2 == 1)
            {
                PowerCutDevice again(disk, cut % 7 + 1);
                FatxResumeResize(again, table, &result);
            }
            if (FatxFindInterruptedResize(disk, &old_size, &new_size))
            {
                ASSERT_TRUE(FatxResumeResize(disk, table, &result)) << result.status;
            }
            EXPECT_EQ(table.size, new_lba);
            resumed++;
        }
        else
        {
            EXPECT_EQ(table.size, base.old_lba);
        }

        const FatxCheckResult check = Check(disk, table.size);
        ASSERT_EQ(check.status, FATX_CHECK_DONE);
        EXPECT_EQ(check.errors, 0u);
        ExpectSameFiles(before, Contents(disk, table.size));
        if (::testing::Test::HasFailure())
        {
            return;
        }
    }
    EXPECT_GT(resumed, 0);
}

} // namespace

TEST(FatxMoveResize, PowerCutAtEveryWriteGrowing)
{
    PowerCutEverywhere(MakeGrow, 1);
}

TEST(FatxMoveResize, PowerCutAtEveryWriteShrinking)
{
    PowerCutEverywhere(MakeShrink, 1);
}

namespace
{

FatxUsage Usage(uint64_t used, uint64_t highest)
{
    FatxUsage usage;
    usage.cluster_bytes = CLUSTER;
    usage.used_clusters = used;
    usage.bad_clusters = 0;
    usage.highest_used = highest;
    return usage;
}

bool Keeps(uint64_t old_lba, const FatxUsage &usage, uint64_t lba)
{
    return FatxClassifyResize(old_lba, usage, lba) != FATX_RESIZE_KIND_FORMAT;
}

} // namespace

TEST(FatxKeepData, ClassifyBySize)
{
    // A nearly empty 64 MB partition: the FAT keeps its size up to 95.99 MB,
    // grows by a whole cluster at 192 MB, and by only 12 KB at 190 MB
    const uint64_t old_lba = 64 * MB / 512;
    const FatxUsage empty = Usage(1, 1);
    EXPECT_EQ(FatxClassifyResize(old_lba, empty, 80 * MB / 512), FATX_RESIZE_KIND_IN_PLACE);
    EXPECT_EQ(FatxClassifyResize(old_lba, empty, 192 * MB / 512), FATX_RESIZE_KIND_MOVES);
    EXPECT_EQ(FatxClassifyResize(old_lba, empty, 190 * MB / 512), FATX_RESIZE_KIND_FORMAT);
    EXPECT_EQ(FatxKeepingSizeAbove(old_lba, empty, 190 * MB / 512), 192 * MB / 512);
    EXPECT_EQ(FatxKeepingSizeBelow(old_lba, empty, 190 * MB / 512), 96 * MB / 512 - 8);
}

TEST(FatxKeepData, ShrinkingInPlaceNeedsTheEndFree)
{
    // 90 MB: the FAT keeps its size down to 64 MB. Cluster 5000 is in use,
    // which 80 MB (5119 clusters) still holds and 70 MB (4479) doesn't.
    const uint64_t old_lba = 90 * MB / 512;
    const FatxUsage usage = Usage(10, 5000);
    EXPECT_EQ(FatxClassifyResize(old_lba, usage, 80 * MB / 512), FATX_RESIZE_KIND_IN_PLACE);
    EXPECT_EQ(FatxClassifyResize(old_lba, usage, 70 * MB / 512), FATX_RESIZE_KIND_FORMAT);

    // The nearest size up from 70 MB is just big enough for cluster 5000
    const uint64_t above = FatxKeepingSizeAbove(old_lba, usage, 70 * MB / 512);
    EXPECT_TRUE(Keeps(old_lba, usage, above));
    EXPECT_FALSE(Keeps(old_lba, usage, above - 8));
    EXPECT_GT(FatxLayoutFor(above, CLUSTER).cluster_limit, 5000u);
}

TEST(FatxKeepData, ShrinkingWithMovesNeedsRoomForTheData)
{
    // 400 MB to 150 MB keeps 9596 clusters in place, one of them spare
    const uint64_t old_lba = 400 * MB / 512;
    EXPECT_EQ(FatxClassifyResize(old_lba, Usage(9000, 20000), 150 * MB / 512), FATX_RESIZE_KIND_MOVES);
    EXPECT_EQ(FatxClassifyResize(old_lba, Usage(9595, 20000), 150 * MB / 512), FATX_RESIZE_KIND_MOVES);
    EXPECT_EQ(FatxClassifyResize(old_lba, Usage(9596, 20000), 150 * MB / 512), FATX_RESIZE_KIND_FORMAT);

    // With 9700 in use, 150 MB is too small: the next size up holds them
    const FatxUsage full = Usage(9700, 20000);
    const uint64_t above = FatxKeepingSizeAbove(old_lba, full, 150 * MB / 512);
    EXPECT_GT(above, 150 * MB / 512);
    EXPECT_TRUE(Keeps(old_lba, full, above));
    EXPECT_FALSE(Keeps(old_lba, full, above - 8));
}

TEST(FatxKeepData, GrowingStaysWithinTheClusterSizeLimit)
{
    // 16 KB clusters stop at 256 GB, whatever the FAT would allow
    const uint64_t old_lba = 0x1F000000;
    const FatxUsage usage = Usage(1000, 1000);
    const uint64_t below = FatxKeepingSizeBelow(old_lba, usage, 0x30000000);
    EXPECT_NE(below, 0u);
    EXPECT_LE(below, 0x20000000u);
    EXPECT_LE(ClusterSizeKbFor(below), 16u);
    EXPECT_TRUE(Keeps(old_lba, usage, below));
    EXPECT_EQ(FatxKeepingSizeAbove(old_lba, usage, 0x30000000), 0u);
    EXPECT_FALSE(Keeps(old_lba, usage, 0x30000000));
}

TEST(FatxKeepData, NearestSizesAreTheNearest)
{
    // Every 4 KB step checked by hand for requests all over the place
    const uint64_t old_lba = 100 * MB / 512;
    const FatxUsage usage = Usage(3000, 5500);
    const uint64_t last = 420 * MB / 512;
    std::vector<bool> keeps((size_t)(last / 8 + 1));
    for (uint64_t lba = 8; lba <= last; lba += 8)
    {
        keeps[(size_t)(lba / 8)] = Keeps(old_lba, usage, lba);
    }

    for (uint64_t request = 5 * MB / 512; request <= 400 * MB / 512; request += 3 * MB / 512 + 8)
    {
        SCOPED_TRACE("request " + std::to_string(request));
        uint64_t expect_below = 0;
        for (uint64_t lba = request & ~7ULL; lba >= 8; lba -= 8)
        {
            if (keeps[(size_t)(lba / 8)])
            {
                expect_below = lba;
                break;
            }
        }
        uint64_t expect_above = 0;
        for (uint64_t lba = (request + 7) & ~7ULL; lba <= last; lba += 8)
        {
            if (keeps[(size_t)(lba / 8)])
            {
                expect_above = lba;
                break;
            }
        }
        EXPECT_EQ(FatxKeepingSizeBelow(old_lba, usage, request), expect_below);
        if (expect_above != 0)
        {
            EXPECT_EQ(FatxKeepingSizeAbove(old_lba, usage, request), expect_above);
        }
    }
}

TEST(FatxKeepData, ReadUsage)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    FatxWriter writer(device, lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    AddFile(writer, writer.RootCluster(), "a", CLUSTER * 3, 1); // Clusters 2 to 4
    writer.SetFat(3000, FATX_FAT_END);                            // Lost, still in use
    writer.SetFat(3001, FATX_FAT_BAD);
    ASSERT_TRUE(writer.Flush());

    FatxUsage usage;
    ASSERT_TRUE(FatxReadUsage(device, lba, &usage));
    EXPECT_EQ(usage.cluster_bytes, CLUSTER);
    EXPECT_EQ(usage.used_clusters, 1u + 3u + 1u); // Root, a, the lost one
    EXPECT_EQ(usage.bad_clusters, 1u);
    EXPECT_EQ(usage.highest_used, 3000u);

    SparseDevice blank;
    EXPECT_FALSE(FatxReadUsage(blank, lba, &usage));
}

TEST(FatxKeepData, AgreesWithTheResize)
{
    // What the editor says from the usage must be what the resize then does
    Case c;
    MakeShrink(c);
    FatxUsage usage;
    ASSERT_TRUE(FatxReadUsage(c.device, c.old_lba, &usage));

    // Every MB, so all three answers come up
    int seen[3] = {0, 0, 0};
    for (uint64_t mb = 30; mb <= 420; mb++)
    {
        const uint64_t lba = mb * MB / HDD_SECTOR_SIZE;
        SCOPED_TRACE(std::to_string(mb) + " MB");
        const FatxResizeKind kind = FatxClassifyResize(c.old_lba, usage, lba);
        seen[kind]++;
        if (kind == FATX_RESIZE_KIND_IN_PLACE)
        {
            FatxResizeResult *resize = new FatxResizeResult;
            FatxCheckResize(c.device, c.old_lba, lba, resize);
            EXPECT_EQ(resize->status, FATX_RESIZE_OK);
            delete resize;
        }
        else
        {
            SizeRecord table(c.old_lba);
            FatxMoveResult *move = new FatxMoveResult;
            FatxResizeWithMoves(c.device, c.old_lba, lba, table, false, move);
            const bool planned = move->status == FATX_MOVE_PLANNED && move->new_lba_size == lba;
            EXPECT_EQ(planned, kind == FATX_RESIZE_KIND_MOVES) << "status " << move->status;
            delete move;
        }
    }
    EXPECT_GT(seen[FATX_RESIZE_KIND_FORMAT], 0);
    EXPECT_GT(seen[FATX_RESIZE_KIND_IN_PLACE], 0);
    EXPECT_GT(seen[FATX_RESIZE_KIND_MOVES], 0);
}

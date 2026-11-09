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

TEST(FatxMoveResize, Classify)
{
    // 64 MB with 16 KB clusters: the FAT keeps its size up to 95.99 MB, grows
    // by a whole cluster at 192 MB, and by only 12 KB at 190 MB
    const uint64_t old_lba = 64 * MB / 512;
    EXPECT_EQ(FatxClassifyResize(old_lba, CLUSTER, 80 * MB / 512), FATX_RESIZE_KIND_IN_PLACE);
    EXPECT_EQ(FatxClassifyResize(old_lba, CLUSTER, 192 * MB / 512), FATX_RESIZE_KIND_MOVES);
    EXPECT_EQ(FatxClassifyResize(old_lba, CLUSTER, 190 * MB / 512), FATX_RESIZE_KIND_FORMAT);
    EXPECT_EQ(FatxMoveResizeNextUp(old_lba, CLUSTER, 190 * MB / 512), 192 * MB / 512);
    EXPECT_EQ(FatxMoveResizeTarget(old_lba, CLUSTER, 190 * MB / 512), 96 * MB / 512 - 8);

    // Too big for 16 KB clusters
    EXPECT_EQ(FatxClassifyResize(0x1F000000, CLUSTER, 0x30000000), FATX_RESIZE_KIND_FORMAT);
}

// Tests that TRIM only ever covers free clusters

#include <gtest/gtest.h>
#include "fatx_check.hpp"
#include "fatx_trim.hpp"
#include "fatx_writer.hpp"
#include "test_images.hpp"

namespace
{

// The cluster numbers the ranges cover
std::vector<uint32_t> Covered(const std::vector<FatxRange> &ranges, const FatxLayout &layout, uint32_t cluster_bytes)
{
    std::vector<uint32_t> clusters;
    for (const FatxRange &range : ranges)
    {
        EXPECT_EQ((range.offset - layout.data_offset) % cluster_bytes, 0u);
        EXPECT_EQ(range.length % cluster_bytes, 0u);
        const uint32_t first = (uint32_t)((range.offset - layout.data_offset) / cluster_bytes) + 1;
        for (uint32_t c = 0; c < range.length / cluster_bytes; c++)
        {
            clusters.push_back(first + c);
        }
    }
    return clusters;
}

} // namespace

TEST(FatxTrim, ExactlyTheFreeClusters)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    FatxWriter writer(device, lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    AddFile(writer, writer.RootCluster(), "a", CLUSTER * 3, 1);
    writer.SetNextFree(1000);
    AddFile(writer, writer.RootCluster(), "b", CLUSTER * 2, 2);
    writer.SetFat(2000, FATX_FAT_END); // Lost, so not trimmed
    writer.SetFat(2001, FATX_FAT_BAD);
    ASSERT_TRUE(writer.Flush());

    std::vector<FatxRange> ranges;
    FatxCheckResult check;
    ASSERT_TRUE(FatxFreeRanges(device, lba * HDD_SECTOR_SIZE, &ranges, &check));
    EXPECT_EQ(ranges.size(), 3u); // Gaps: after a, between b and the lost one, after the bad one

    const std::vector<uint32_t> covered = Covered(ranges, writer.Layout(), CLUSTER);
    std::vector<bool> trimmed(writer.Layout().cluster_limit, false);
    for (uint32_t c : covered)
    {
        ASSERT_LT(c, writer.Layout().cluster_limit);
        EXPECT_FALSE(trimmed[c]) << "cluster " << c << " twice";
        trimmed[c] = true;
    }
    for (uint32_t c = 1; c < writer.Layout().cluster_limit; c++)
    {
        EXPECT_EQ(trimmed[c], writer.GetFat(c) == FATX_FAT_FREE) << "cluster " << c;
    }
}

TEST(FatxTrim, NothingWithErrors)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice device;
    FatxWriter writer(device, lba * HDD_SECTOR_SIZE, CLUSTER);
    ASSERT_TRUE(writer.Format());
    uint32_t first;
    AddFile(writer, writer.RootCluster(), "a", CLUSTER * 3, 1, &first);
    writer.SetFat(first, 500); // Chain into a free cluster, which must not be trimmed
    ASSERT_TRUE(writer.Flush());

    std::vector<FatxRange> ranges;
    FatxCheckResult check;
    EXPECT_FALSE(FatxFreeRanges(device, lba * HDD_SECTOR_SIZE, &ranges, &check));
    EXPECT_GT(check.errors, 0u);
    EXPECT_TRUE(ranges.empty());
}

TEST(FatxTrim, NotFatx)
{
    SparseDevice blank;
    std::vector<FatxRange> ranges;
    FatxCheckResult check;
    EXPECT_FALSE(FatxFreeRanges(blank, 64 * MB, &ranges, &check));
    EXPECT_EQ(check.status, FATX_CHECK_NOT_FATX);
}

TEST(FatxTrim, PartitionInsideADisk)
{
    // A partition 1 MB into a disk gives the same ranges as on its own
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    const uint64_t base = 1 * MB;
    SparseDevice disk;
    SparseDevice alone;
    {
        FatxWriter writer(alone, lba * HDD_SECTOR_SIZE, CLUSTER);
        ASSERT_TRUE(writer.Format());
        AddFile(writer, writer.RootCluster(), "a", CLUSTER * 3, 1);
        ASSERT_TRUE(writer.Flush());
    }
    // Copy its first MB in (header, FAT, root and the file are all there)
    std::vector<unsigned char> data(1 * MB);
    ASSERT_TRUE(alone.Read(0, data.data(), (uint32_t)data.size()));
    ASSERT_TRUE(disk.Write(base, data.data(), (uint32_t)data.size()));

    std::vector<FatxRange> from_disk;
    std::vector<FatxRange> from_alone;
    FatxCheckResult check;
    FatxOffsetReader partition(disk, base);
    ASSERT_TRUE(FatxFreeRanges(partition, lba * HDD_SECTOR_SIZE, &from_disk, &check));
    ASSERT_TRUE(FatxFreeRanges(alone, lba * HDD_SECTOR_SIZE, &from_alone, &check));
    ASSERT_EQ(from_disk.size(), from_alone.size());
    for (size_t i = 0; i < from_disk.size(); i++)
    {
        EXPECT_EQ(from_disk[i].offset, from_alone[i].offset);
        EXPECT_EQ(from_disk[i].length, from_alone[i].length);
    }
}

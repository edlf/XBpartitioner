// fatx_format: what the Xbox app now writes instead of XapiFormatFATVolumeEx

#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "fatx_check.hpp"
#include "fatx_format.hpp"
#include "fatx_writer.hpp"
#include "test_images.hpp"

namespace
{

const uint32_t SERIAL = 0x13579BDF;

// Formats, then checks every byte it should have written
void ExpectFreshFormat(uint64_t lba_size, uint32_t cluster_bytes)
{
    SCOPED_TRACE(testing::Message() << (lba_size * HDD_SECTOR_SIZE / MB) << " MB, " << cluster_bytes / 1024 << " KB clusters");
    SparseDevice device;
    ASSERT_TRUE(FatxFormat(device, lba_size, cluster_bytes, SERIAL));
    const FatxLayout layout = FatxLayoutFor(lba_size, cluster_bytes);

    // Header: "FATX", serial, sectors per cluster, root 1, a 16 bit 0, then 0xFF
    std::vector<unsigned char> header(FATX_HEADER_BYTES);
    ASSERT_TRUE(device.Read(0, header.data(), (uint32_t)header.size()));
    uint32_t value;
    memcpy(&value, &header[0], 4);
    EXPECT_EQ(value, (uint32_t)FATX_signature);
    memcpy(&value, &header[4], 4);
    EXPECT_EQ(value, SERIAL);
    memcpy(&value, &header[8], 4);
    EXPECT_EQ(value, cluster_bytes / HDD_SECTOR_SIZE);
    memcpy(&value, &header[12], 4);
    EXPECT_EQ(value, 1u);
    EXPECT_EQ(header[16], 0);
    EXPECT_EQ(header[17], 0);
    for (size_t i = 18; i < header.size(); i++)
    {
        ASSERT_EQ(header[i], 0xFF) << "header byte " << i;
    }

    // FAT: media marker, the root's end, the rest free
    std::vector<unsigned char> fat((size_t)layout.bytes_per_fat);
    ASSERT_TRUE(device.Read(FATX_HEADER_BYTES, fat.data(), (uint32_t)fat.size()));
    const uint32_t width = layout.bytes_per_entry;
    uint32_t entry0 = 0, entry1 = 0;
    memcpy(&entry0, &fat[0], width);
    memcpy(&entry1, &fat[width], width);
    EXPECT_EQ(entry0, width == 2 ? 0xFFF8u : 0xFFFFFFF8u);
    EXPECT_EQ(entry1, width == 2 ? 0xFFFFu : 0xFFFFFFFFu);
    for (size_t i = 2 * width; i < fat.size(); i++)
    {
        ASSERT_EQ(fat[i], 0) << "FAT byte " << i;
    }

    // Root folder: empty
    std::vector<unsigned char> root(cluster_bytes);
    ASSERT_TRUE(device.Read(layout.data_offset, root.data(), cluster_bytes));
    for (size_t i = 0; i < root.size(); i++)
    {
        ASSERT_EQ(root[i], FATX_DIRENT_END) << "root byte " << i;
    }

    // And the check agrees
    FatxCheckResult *check = new FatxCheckResult;
    ASSERT_TRUE(FatxCheck(device, lba_size * HDD_SECTOR_SIZE, check));
    EXPECT_EQ(check->status, FATX_CHECK_DONE);
    EXPECT_EQ(check->errors, 0u);
    EXPECT_EQ(check->warnings, 0u);
    EXPECT_EQ(check->files, 0u);
    EXPECT_EQ(check->directories, 0u);
    EXPECT_EQ(check->cluster_bytes, cluster_bytes);
    EXPECT_EQ(check->used_clusters, 1u); // The root
    EXPECT_EQ(check->layout.bytes_per_fat, layout.bytes_per_fat);
    delete check;
}

} // namespace

// 2 byte and 4 byte FATs at every cluster size the Xbox app uses
TEST(FatxFormat, EveryClusterSize)
{
    for (uint32_t kb = 16; kb <= 1024; kb *= 2)
    {
        ExpectFreshFormat(64 * MB / HDD_SECTOR_SIZE, kb * 1024);
        ExpectFreshFormat(4096 * MB / HDD_SECTOR_SIZE, kb * 1024);
    }
    // Both widths came up: 4 GB is 4 byte entries at 16 KB, 2 byte at 1 MB
    EXPECT_EQ(FatxLayoutFor(4096 * MB / HDD_SECTOR_SIZE, 16 * 1024).bytes_per_entry, 4u);
    EXPECT_EQ(FatxLayoutFor(4096 * MB / HDD_SECTOR_SIZE, 1024 * 1024).bytes_per_entry, 2u);
}

// The stock partitions' FAT sizes, as the Xbox's own format lays them out
// (XBlast hard codes these): data starts right after header and FAT
TEST(FatxFormat, StockPartitionsMatchTheXbox)
{
    struct Stock
    {
        uint64_t sectors;
        uint64_t fat_sectors;
    };
    const Stock stock[] = {
        {0x177000, 192}, // X, Y, Z (cache)
        {0xFA000, 128},  // C
        {0x9896B0, 2448} // E
    };
    for (const Stock &s : stock)
    {
        const FatxLayout layout = FatxLayoutFor(s.sectors, 16 * 1024);
        EXPECT_EQ(layout.bytes_per_fat, s.fat_sectors * HDD_SECTOR_SIZE) << s.sectors;
        EXPECT_EQ(layout.data_offset, (8 + s.fat_sectors) * HDD_SECTOR_SIZE) << s.sectors;
    }
    ExpectFreshFormat(0x9896B0, 16 * 1024);
}

// E, C and the caches resized away from stock: everything follows the size in
// the table, nothing the stock layout fixes
TEST(FatxFormat, StockPartitionsAtOtherSizes)
{
    struct Resized
    {
        const char *name;
        uint64_t stock_sectors;
        uint64_t sectors;
    };
    const Resized resized[] = {
        {"E grown to 20 GB", 0x9896B0, 20ULL * 1024 * MB / HDD_SECTOR_SIZE},
        {"E shrunk, odd size", 0x9896B0, 0x9896B0 / 3 + 5 * LBA_ALIGNMENT},
        {"C grown to 2 GB", 0xFA000, 2048 * MB / HDD_SECTOR_SIZE},
        {"X shrunk to 100 MB", 0x177000, 100 * MB / HDD_SECTOR_SIZE},
        {"Y not a whole number of clusters", 0x177000, 0x177000 + 3 * LBA_ALIGNMENT},
        {"E past 256 GB", 0x9896B0, 0x30000000},
    };
    for (const Resized &r : resized)
    {
        SCOPED_TRACE(r.name);
        const uint32_t cluster_bytes = ClusterSizeKbFor(r.sectors) * 1024;
        const FatxLayout layout = FatxLayoutFor(r.sectors, cluster_bytes);
        const FatxLayout stock = FatxLayoutFor(r.stock_sectors, 16 * 1024);

        // The FAT has an entry for every cluster this size holds
        EXPECT_GE(layout.bytes_per_fat / layout.bytes_per_entry, r.sectors * HDD_SECTOR_SIZE / cluster_bytes);
        EXPECT_EQ(layout.cluster_limit - 1, (r.sectors * HDD_SECTOR_SIZE - layout.data_offset) / cluster_bytes);
        if (r.sectors * HDD_SECTOR_SIZE / cluster_bytes != r.stock_sectors * HDD_SECTOR_SIZE / (16 * 1024))
        {
            EXPECT_NE(layout.cluster_limit, stock.cluster_limit);
        }
        if (r.sectors < 0x30000000)
        {
            ExpectFreshFormat(r.sectors, cluster_bytes);
        }
    }
    // Past 256 GB XBpartitioner picks 32 KB clusters for E as for any other
    EXPECT_EQ(ClusterSizeKbFor(0x30000000), 32u);
}

// A format that stops partway never leaves a FATX header over the wrong FAT:
// the old file system is gone from the first write on
TEST(FatxFormat, StoppingPartwayLeavesNoFileSystem)
{
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    SparseDevice original;
    {
        FatxWriter writer(original, lba * HDD_SECTOR_SIZE, 16 * 1024);
        ASSERT_TRUE(writer.Format(SERIAL));
        std::vector<unsigned char> data(100 * 1024, 7);
        for (int i = 0; i < 5; i++)
        {
            ASSERT_TRUE(writer.AddFile(writer.RootCluster(), ("old" + std::to_string(i)).c_str(), data.data(), (uint32_t)data.size()));
        }
        ASSERT_TRUE(writer.Flush());
    }

    // How many writes a whole format takes
    SparseDevice counted = original;
    PowerCutDevice counter(counted, ~0ULL);
    ASSERT_TRUE(FatxFormat(counter, lba, 32 * 1024, SERIAL));
    const uint64_t total = counter.Writes();
    ASSERT_GE(total, 3u);

    for (uint64_t cut = 0; cut + 1 < total; cut++)
    {
        SparseDevice disk = original;
        PowerCutDevice cutting(disk, cut);
        EXPECT_FALSE(FatxFormat(cutting, lba, 32 * 1024, SERIAL));
        FatxCheckResult *check = new FatxCheckResult;
        FatxCheck(disk, lba * HDD_SECTOR_SIZE, check);
        EXPECT_EQ(check->status, FATX_CHECK_NOT_FATX) << "cut at write " << cut;
        delete check;
    }
}

TEST(FatxFormat, Refusals)
{
    SparseDevice device;
    const uint64_t lba = 64 * MB / HDD_SECTOR_SIZE;
    EXPECT_FALSE(FatxFormat(device, lba, 0, SERIAL));
    EXPECT_FALSE(FatxFormat(device, lba, 24 * 1024, SERIAL));   // Not a power of two
    EXPECT_FALSE(FatxFormat(device, lba, 2048 * 1024, SERIAL)); // Over 1 MB
    EXPECT_FALSE(FatxFormat(device, 16, 16 * 1024, SERIAL));    // No room for the root
}

// fatxtool make and recluster format through it too (FatxWriter::Format)
TEST(FatxFormat, WriterFormatsTheSameWay)
{
    const uint64_t lba = 256 * MB / HDD_SECTOR_SIZE;
    SparseDevice direct, through_writer;
    ASSERT_TRUE(FatxFormat(direct, lba, 16 * 1024, SERIAL));
    FatxWriter writer(through_writer, lba * HDD_SECTOR_SIZE, 16 * 1024);
    ASSERT_TRUE(writer.Format(SERIAL));
    const FatxLayout layout = FatxLayoutFor(lba, 16 * 1024);
    const uint64_t length = layout.data_offset + 16 * 1024;
    std::vector<unsigned char> a((size_t)length), b((size_t)length);
    ASSERT_TRUE(direct.Read(0, a.data(), (uint32_t)length));
    ASSERT_TRUE(through_writer.Read(0, b.data(), (uint32_t)length));
    EXPECT_EQ(a, b);
}

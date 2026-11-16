// fatxgui's stock layout: the stock partitions on an image without a table

#define NOMINMAX
#include <gtest/gtest.h>
#include "stock_layout.hpp"
#include "test_images.hpp"

using namespace fatxgui;

namespace
{

const int STOCK_C = 1;
const int STOCK_X = 2;

void WriteFatxSignature(FatxDevice &disk, uint64_t lba)
{
    unsigned char sector[HDD_SECTOR_SIZE] = {};
    const uint32_t signature = FATX_signature;
    memcpy(sector, &signature, sizeof(signature));
    ASSERT_TRUE(disk.Write(lba * HDD_SECTOR_SIZE, sector, sizeof(sector)));
}

std::vector<Part> StockParts(FatxDevice &disk)
{
    std::vector<Part> parts;
    read_stock_parts(disk, &parts);
    return parts;
}

} // namespace

TEST(StockLayout, FoundWhenAStockPartitionHoldsFatx)
{
    SparseDevice disk;
    WriteFatxSignature(disk, XBOX_SYSPART_LBA_START);
    std::vector<Part> parts;
    EXPECT_TRUE(read_stock_parts(disk, &parts));
}

TEST(StockLayout, NotFoundOnABlankDisk)
{
    SparseDevice disk;
    std::vector<Part> parts;
    EXPECT_FALSE(read_stock_parts(disk, &parts));
}

TEST(StockLayout, NotFoundForFatxOnlyAtSectorZero)
{
    SparseDevice disk;
    WriteFatxSignature(disk, 0);
    std::vector<Part> parts;
    EXPECT_FALSE(read_stock_parts(disk, &parts));
}

TEST(StockLayout, ListsTheFiveStockPartitions)
{
    SparseDevice disk;
    WriteFatxSignature(disk, XBOX_SWAPPART1_LBA_START);
    EXPECT_EQ(StockParts(disk).size(), (size_t)XBOX_STANDARD_PARTITION_COUNT);
}

TEST(StockLayout, PlacesCAtTheStockOffset)
{
    SparseDevice disk;
    WriteFatxSignature(disk, XBOX_SYSPART_LBA_START);
    const Part c = StockParts(disk)[STOCK_C];
    EXPECT_EQ(c.start, XBOX_SYSPART_LBA_START);
    EXPECT_EQ(c.sectors, XBOX_SYSPART_LBA_SIZE);
}

TEST(StockLayout, PartitionsCannotGrow)
{
    SparseDevice disk;
    WriteFatxSignature(disk, XBOX_SWAPPART1_LBA_START);
    const Part x = StockParts(disk)[STOCK_X];
    EXPECT_EQ(x.max_sectors, x.sectors);
}

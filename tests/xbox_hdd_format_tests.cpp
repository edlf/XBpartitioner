// Formatting a whole drive as an Xbox hard drive on the PC

#include <gtest/gtest.h>
#include <memory>
#include "drive_survey.hpp"
#include "pc_image.hpp"
#include "test_images.hpp"
#include "xbox_hdd_format.hpp"

namespace
{

const uint64_t GB = 1024ULL * 1024 * 1024;

XboxPartitionTable TableAt(FatxDevice &disk, uint64_t lba)
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    EXPECT_TRUE(disk.Read(lba * HDD_SECTOR_SIZE, &table, sizeof(table)));
    return table;
}

bool ChecksClean(FatxDevice &disk, const PartitionTableEditor &table, int partition)
{
    FatxOffsetDevice device(disk, table.GetPartitionStart(partition) * HDD_SECTOR_SIZE);
    std::unique_ptr<FatxCheckResult> result = std::make_unique<FatxCheckResult>();
    FatxCheck(device, table.GetPartitionSize(partition) * HDD_SECTOR_SIZE, result.get());
    return result->status == FATX_CHECK_DONE && result->errors == 0 && result->warnings == 0;
}

const XboxHddSpec F_G_V4 = {XBOX_HDD_F_G, true, 0x1234};

} // namespace

TEST(FormatXboxHdd, EveryPartitionChecksClean)
{
    SparseDevice disk;
    const uint64_t bytes = 20 * GB;
    ASSERT_TRUE(format_xbox_hdd(disk, bytes, F_G_V4));

    const XboxPartitionTable written = TableAt(disk, 0);
    ASSERT_TRUE(PartitionTableEditor::HasMagic(written));
    EXPECT_EQ(written.Version, XBOX_TABLE_VERSION_LBA48_V4);
    const PartitionTableEditor table(written, bytes / HDD_SECTOR_SIZE, true, true);
    EXPECT_EQ(table.GetActivePartitionCount(), 7);
    for (int i = 0; i < 7; i++)
    {
        EXPECT_TRUE(ChecksClean(disk, table, i)) << "partition " << i + 1;
    }
}

TEST(FormatXboxHdd, WritesTheBackupInBothPlaces)
{
    SparseDevice disk;
    const uint64_t sectors = 20 * GB / HDD_SECTOR_SIZE;
    ASSERT_TRUE(format_xbox_hdd(disk, sectors * HDD_SECTOR_SIZE, F_G_V4));
    EXPECT_TRUE(PartitionTableEditor::HasMagic(TableAt(disk, sectors - 1)));
    EXPECT_TRUE(PartitionTableEditor::HasMagic(TableAt(disk, sectors - LBA_ALIGNMENT)));
}

TEST(FormatXboxHdd, AStockSizedDriveHasNoBackupInE)
{
    SparseDevice disk;
    const XboxHddSpec stock = {XBOX_HDD_STOCK, true, 1};
    ASSERT_TRUE(format_xbox_hdd(disk, XBOX_HDD_MIN_BYTES, stock));
    const PartitionTableEditor table(TableAt(disk, 0), XBOX_HDD_MIN_BYTES / HDD_SECTOR_SIZE, true, true);
    EXPECT_TRUE(ChecksClean(disk, table, 0)); // E, which reaches the last sector
}

TEST(FormatXboxHdd, ATooSmallDriveIsNotTouched)
{
    SparseDevice disk;
    const XboxHddSpec stock = {XBOX_HDD_STOCK, true, 1};
    EXPECT_FALSE(format_xbox_hdd(disk, XBOX_HDD_MIN_BYTES - LBA_ALIGNMENT * HDD_SECTOR_SIZE, stock));
    EXPECT_FALSE(PartitionTableEditor::HasMagic(TableAt(disk, 0)));
}

TEST(FormatXboxHdd, AnInterruptedFormatLeavesNoTable)
{
    // An old Xbox table, then cuts right after the first write and right
    // before the last one: neither leaves a table that points at half
    // formatted partitions
    SparseDevice counting_disk;
    PowerCutDevice counting(counting_disk, ~0ULL);
    ASSERT_TRUE(format_xbox_hdd(counting, 20 * GB, F_G_V4));
    const uint64_t writes = counting.Writes();

    for (const uint64_t cut : {(uint64_t)1, writes - 1})
    {
        SparseDevice disk;
        const XboxPartitionTable old = xbox_hdd_table(20 * GB / HDD_SECTOR_SIZE, F_G_V4).GetPartitionTable();
        ASSERT_TRUE(disk.Write(0, &old, sizeof(old)));
        PowerCutDevice device(disk, cut);
        EXPECT_FALSE(format_xbox_hdd(device, 20 * GB, F_G_V4));
        EXPECT_FALSE(PartitionTableEditor::HasMagic(TableAt(disk, 0))) << "cut after write " << cut;
    }
}

TEST(XboxHddTable, AV3TableKeepsPartitionsTo1Tb)
{
    const XboxHddSpec v3 = {XBOX_HDD_F, false, 1};
    const PartitionTableEditor table = xbox_hdd_table(3ULL * 1024 * GB / HDD_SECTOR_SIZE, v3);
    EXPECT_EQ(table.GetPartitionTable().Version, XBOX_TABLE_VERSION_LBA48_V3);
    EXPECT_LE(table.GetPartitionSize(5), (uint64_t)LBASIZE_LBA48_V3_MAX);
}

TEST(XboxFormatRefusal, NeverADriveWithAWindowsVolume)
{
    fatxgui::DriveInfo drive;
    drive.bytes = 500 * GB;
    drive.mounted_volumes = "D:";
    EXPECT_FALSE(fatxgui::xbox_format_refusal(drive).empty());
}

TEST(XboxFormatRefusal, NotADriveSmallerThanTheStockLayout)
{
    fatxgui::DriveInfo drive;
    drive.bytes = XBOX_HDD_MIN_BYTES - HDD_SECTOR_SIZE;
    EXPECT_FALSE(fatxgui::xbox_format_refusal(drive).empty());
}

TEST(XboxFormatRefusal, AnyOtherDriveMayBeFormatted)
{
    fatxgui::DriveInfo drive;
    drive.bytes = 500 * GB;
    EXPECT_TRUE(fatxgui::xbox_format_refusal(drive).empty());
}

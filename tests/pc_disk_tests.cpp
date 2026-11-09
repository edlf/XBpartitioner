// The PC's writes to whole disks: the table writer (sector 0 and the backup)
// and WindowsDisk, which fatxgui uses to repair and resize Xbox drives. A
// file stands in for the drive; it's opened the same way (unbuffered, write
// through), so the 4 KB block handling is what a drive gets.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "fatx_check.hpp"
#include "fatx_resize.hpp"
#include "fatx_trim.hpp"
#include "fatx_writer.hpp"
#include "pc_image.hpp"
#include "test_images.hpp"

namespace
{

// A table with partition 6 (F) at start for sectors
XboxPartitionTable MakeTable(uint64_t start, uint64_t sectors)
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    memcpy(table.Magic, LBA48_Partition_Table_Magic, sizeof(table.Magic));
    table.Version = XBOX_TABLE_VERSION_LBA48_V4;
    XboxPartitionTableEntry &entry = table.TableEntries[5];
    memcpy(entry.Name, "XBOX F          ", sizeof(entry.Name));
    entry.Flags = PE_PARTFLAGS_IN_USE;
    entry.LBAStart = (uint32_t)start;
    entry.LBASize = (uint32_t)sectors;
    return table;
}

uint64_t EntrySize(const XboxPartitionTable &table)
{
    return table.TableEntries[5].LBASize | (uint64_t)table.TableEntries[5].LBASize_high << 32;
}

// A temporary file, removed afterwards
class TempFile
{
  public:
    explicit TempFile(const char *name)
    {
        char dir[MAX_PATH];
        GetTempPathA(MAX_PATH, dir);
        m_path = std::string(dir) + name + "_" + std::to_string(GetCurrentProcessId()) + ".img";
    }
    ~TempFile()
    {
        DeleteFileA(m_path.c_str());
    }
    const char *Path() const
    {
        return m_path.c_str();
    }

  private:
    std::string m_path;
};

} // namespace

TEST(DiskTable, WritesSectorZeroAndTheBackups)
{
    const uint64_t disk_bytes = 1024 * MB;
    const uint64_t sectors = disk_bytes / HDD_SECTOR_SIZE;
    SparseDevice disk;
    const XboxPartitionTable old_table = MakeTable(2048, 64 * MB / HDD_SECTOR_SIZE);
    ASSERT_TRUE(disk.Write(0, &old_table, sizeof(old_table)));

    // A Titan backup in the last sector, and something else in the last 4K block
    XboxPartitionTable titan = old_table;
    titan.Version = XBOX_TABLE_VERSION_TITAN;
    ASSERT_TRUE(disk.Write((sectors - 1) * HDD_SECTOR_SIZE, &titan, sizeof(titan)));
    XboxPartitionTable other = MakeTable(4096, 8);
    ASSERT_TRUE(disk.Write((sectors - LBA_ALIGNMENT) * HDD_SECTOR_SIZE, &other, sizeof(other)));

    DiskTable table(disk, disk_bytes, 5);
    const uint64_t new_size = (5ULL << 32) + 2048; // Needs the high bits
    ASSERT_TRUE(table.SetSize(new_size));

    XboxPartitionTable main, backup, untouched;
    ASSERT_TRUE(disk.Read(0, &main, sizeof(main)));
    ASSERT_TRUE(disk.Read((sectors - 1) * HDD_SECTOR_SIZE, &backup, sizeof(backup)));
    ASSERT_TRUE(disk.Read((sectors - LBA_ALIGNMENT) * HDD_SECTOR_SIZE, &untouched, sizeof(untouched)));
    EXPECT_EQ(EntrySize(main), new_size);
    EXPECT_EQ(EntrySize(backup), new_size);
    EXPECT_EQ(backup.Version, XBOX_TABLE_VERSION_TITAN); // Kept
    EXPECT_EQ(memcmp(&untouched, &other, sizeof(other)), 0);
}

TEST(DiskTable, RefusesWithoutATable)
{
    SparseDevice disk;
    DiskTable table(disk, 64 * MB, 5);
    EXPECT_FALSE(table.SetSize(2048));
}

TEST(WindowsDisk, WritesPartOfABlockAndKeepsTheRest)
{
    TempFile temp("fatx_windisk_part");
    std::vector<unsigned char> pattern(64 * 1024);
    for (size_t i = 0; i < pattern.size(); i++)
    {
        pattern[i] = (unsigned char)(i * 7 + 3);
    }
    FILE *file = NULL;
    ASSERT_EQ(fopen_s(&file, temp.Path(), "wb"), 0);
    fwrite(pattern.data(), 1, pattern.size(), file);
    fclose(file);

    {
        WindowsDisk disk;
        unsigned long error = 0;
        ASSERT_TRUE(disk.Open(temp.Path(), true, &error)) << error;
        EXPECT_EQ(disk.Size(), pattern.size());

        // Starts and ends inside blocks, and crosses two of them
        std::vector<unsigned char> data(10000, 0xAB);
        ASSERT_TRUE(disk.Write(1000, data.data(), (uint32_t)data.size()));
        ASSERT_TRUE(disk.Sync());

        std::vector<unsigned char> back(10000);
        ASSERT_TRUE(disk.Read(1000, back.data(), (uint32_t)back.size()));
        EXPECT_EQ(back, data);
    }

    std::vector<unsigned char> after(pattern.size());
    ASSERT_EQ(fopen_s(&file, temp.Path(), "rb"), 0);
    ASSERT_EQ(fread(after.data(), 1, after.size(), file), after.size());
    fclose(file);
    for (size_t i = 0; i < after.size(); i++)
    {
        const unsigned char expected = (i >= 1000 && i < 11000) ? 0xAB : pattern[i];
        ASSERT_EQ(after[i], expected) << "at byte " << i;
    }
}

TEST(WindowsDisk, RefusesToWriteWhenOpenedForReading)
{
    TempFile temp("fatx_windisk_ro");
    FILE *file = NULL;
    ASSERT_EQ(fopen_s(&file, temp.Path(), "wb"), 0);
    std::vector<unsigned char> zeros(8192, 0);
    fwrite(zeros.data(), 1, zeros.size(), file);
    fclose(file);

    WindowsDisk disk;
    unsigned long error = 0;
    ASSERT_TRUE(disk.Open(temp.Path(), false, &error)) << error;
    const unsigned char byte = 1;
    EXPECT_FALSE(disk.Write(0, &byte, 1));
}

// What fatxgui does on a drive: a resize that moves clusters, through
// WindowsDisk, with the size going into the disk's table and its backup
TEST(WindowsDisk, ResizesAPartitionOnTheDisk)
{
    TempFile temp("fatx_windisk_resize");
    const uint64_t start = 2048;
    const uint64_t old_lba = 64 * MB / HDD_SECTOR_SIZE;
    const uint64_t disk_bytes = MB + 400 * MB;
    const uint64_t sectors = disk_bytes / HDD_SECTOR_SIZE;

    // The disk: a table, a backup in the last sector, and a filled partition
    {
        FILE *file = NULL;
        ASSERT_EQ(fopen_s(&file, temp.Path(), "w+b"), 0);
        ASSERT_EQ(_chsize_s(_fileno(file), (__int64)disk_bytes), 0);
        FileDevice image(file, 0);
        const XboxPartitionTable table = MakeTable(start, old_lba);
        ASSERT_TRUE(image.Write(0, &table, sizeof(table)));
        ASSERT_TRUE(image.Write((sectors - 1) * HDD_SECTOR_SIZE, &table, sizeof(table)));
        FatxOffsetDevice partition(image, start * HDD_SECTOR_SIZE);
        FatxWriter writer(partition, old_lba * HDD_SECTOR_SIZE, CLUSTER);
        ASSERT_TRUE(writer.Format());
        std::vector<unsigned char> data(100 * 1024);
        for (int i = 0; i < 20; i++)
        {
            memset(data.data(), i + 1, data.size());
            ASSERT_TRUE(writer.AddFile(writer.RootCluster(), ("file" + std::to_string(i)).c_str(), data.data(), (uint32_t)data.size()));
        }
        ASSERT_TRUE(writer.Flush());
        fclose(file);
    }

    uint64_t new_lba = 0;
    {
        WindowsDisk disk;
        unsigned long error = 0;
        ASSERT_TRUE(disk.Open(temp.Path(), true, &error)) << error;
        FatxOffsetDevice partition(disk, start * HDD_SECTOR_SIZE);
        DiskTable table(disk, disk.Size(), 5);
        FatxMoveResult *move = new FatxMoveResult;
        ASSERT_TRUE(FatxResizeWithMoves(partition, old_lba, 352 * MB / HDD_SECTOR_SIZE, table, true, move)) << move->status;
        EXPECT_EQ(move->status, FATX_MOVE_DONE);
        EXPECT_GT(move->clusters_moved, 0u);
        new_lba = move->new_lba_size;
        delete move;

        FatxCheckResult *check = new FatxCheckResult;
        FatxCheck(partition, new_lba * HDD_SECTOR_SIZE, check);
        EXPECT_EQ(check->status, FATX_CHECK_DONE);
        EXPECT_EQ(check->errors, 0u);
        EXPECT_EQ(check->files, 20u);
        delete check;
    }

    // Both tables have the new size
    FILE *file = NULL;
    ASSERT_EQ(fopen_s(&file, temp.Path(), "rb"), 0);
    FileDevice image(file, 0);
    XboxPartitionTable main, backup;
    ASSERT_TRUE(image.Read(0, &main, sizeof(main)));
    ASSERT_TRUE(image.Read((sectors - 1) * HDD_SECTOR_SIZE, &backup, sizeof(backup)));
    fclose(file);
    EXPECT_EQ(EntrySize(main), new_lba);
    EXPECT_EQ(EntrySize(backup), new_lba);
}

// What fatxtool and fatxgui open: a \\.\ path is a drive (a file reached that
// way stands in for one), anything else an image
TEST(DiskTarget, OpensADevicePathAsADrive)
{
    TempFile temp("fatx_target_drive");
    {
        FILE *file = NULL;
        ASSERT_EQ(fopen_s(&file, temp.Path(), "w+b"), 0);
        ASSERT_EQ(_chsize_s(_fileno(file), (__int64)(64 * MB)), 0);
        const XboxPartitionTable table = MakeTable(2048, 16 * MB / HDD_SECTOR_SIZE);
        fwrite(&table, 1, sizeof(table), file);
        fclose(file);
    }

    DiskTarget target;
    std::string error;
    ASSERT_TRUE(target.Open(std::string("\\\\.\\") + temp.Path(), true, false, &error)) << error;
    EXPECT_TRUE(target.IsDrive());
    EXPECT_EQ(target.File(), nullptr);
    EXPECT_EQ(target.Size(), 64 * MB);
    XboxPartitionTable table;
    ASSERT_TRUE(target.ReadTable(&table));

    // A drive has no "partition image" length to change
    EXPECT_EQ(target.Table(-1), nullptr);
    std::unique_ptr<FatxResizeTable> writer = target.Table(5);
    ASSERT_NE(writer, nullptr);
    ASSERT_TRUE(writer->SetSize(32 * MB / HDD_SECTOR_SIZE));
    ASSERT_TRUE(target.ReadTable(&table));
    EXPECT_EQ(EntrySize(table), 32 * MB / HDD_SECTOR_SIZE);
}

TEST(DiskTarget, OpensAnImage)
{
    TempFile temp("fatx_target_image");
    FILE *file = NULL;
    ASSERT_EQ(fopen_s(&file, temp.Path(), "wb"), 0);
    std::vector<unsigned char> zeros(1 * MB, 0);
    fwrite(zeros.data(), 1, zeros.size(), file);
    fclose(file);

    DiskTarget target;
    std::string error;
    ASSERT_TRUE(target.Open(temp.Path(), false, false, &error)) << error;
    EXPECT_FALSE(target.IsDrive());
    EXPECT_NE(target.File(), nullptr);
    XboxPartitionTable table;
    EXPECT_FALSE(target.ReadTable(&table)); // No table: a partition image
    EXPECT_NE(target.Table(-1), nullptr);   // Its size is the file's length
}

TEST(DiskTarget, ExplainsWhatCantBeOpened)
{
    DiskTarget drive;
    std::string error;
    EXPECT_FALSE(drive.Open("\\\\.\\PhysicalDrive97", false, false, &error));
    EXPECT_NE(error.find("PhysicalDrive97"), std::string::npos) << error;

    DiskTarget image;
    EXPECT_FALSE(image.Open("Z:\\no\\such\\image.img", false, false, &error));
    EXPECT_NE(error.find("Can't open"), std::string::npos) << error;
}

// Growing on a disk with a backup table stops before the last 4K block,
// wherever XBpartitioner put the backup (last sector or that block)
TEST(DiskTarget, GrowingKeepsClearOfTheBackup)
{
    const uint64_t total = 20ULL * 1024 * 1024 * 1024 / HDD_SECTOR_SIZE + 5; // Not a whole number of 4K blocks
    XboxPartitionTable table = MakeTable(XBOX_PART6_LBA_START, 8 * MB / HDD_SECTOR_SIZE);
    const XboxPartitionTableManipulator editor(table, total, true, true);
    const uint64_t end = XBOX_PART6_LBA_START + 8 * MB / HDD_SECTOR_SIZE + editor.GetGrowthLimit(5);
    const uint64_t last_full_block = (total & ~(uint64_t)(LBA_ALIGNMENT - 1)) - LBA_ALIGNMENT;
    EXPECT_LE(end, last_full_block);
    EXPECT_LT(end, total - 1);
}

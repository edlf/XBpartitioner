// partition_format: the table's text columns and the version-blind comparison

#include <gtest/gtest.h>
#include <string.h>
#include <wchar.h>
#include "partition_format.h"

namespace
{

const uint64_t SECTORS_PER_MB = 0x800;
const uint64_t SECTORS_PER_GB = 0x200000;
const uint64_t SECTORS_PER_TB = 0x80000000;

std::wstring HumanSize(uint64_t sectors)
{
    wchar_t text[HUMAN_SIZE_TEXT_COUNT];
    FormatHumanSize(sectors, text);
    return text;
}

std::wstring TableLba(uint64_t lba)
{
    wchar_t text[TABLE_LBA_TEXT_COUNT];
    FormatTableLba(lba, text);
    return text;
}

XboxPartitionTable StockTable()
{
    PartitionTableEditor editor(16 * SECTORS_PER_GB, true, false);
    return editor.GetPartitionTable();
}

} // namespace

TEST(PartitionLetter, StandardPartitionsUseStockLetters)
{
    const wchar_t letters[XBOX_STANDARD_PARTITION_COUNT + 1] = {
        PartitionLetter(0), PartitionLetter(1), PartitionLetter(2), PartitionLetter(3), PartitionLetter(4), L'\0'};
    EXPECT_STREQ(letters, L"ECXYZ");
}

TEST(PartitionLetter, FirstExtendedPartitionIsF)
{
    EXPECT_EQ(PartitionLetter(XBOX_STANDARD_PARTITION_COUNT), L'F');
}

TEST(PartitionLetter, LastPartitionIsN)
{
    EXPECT_EQ(PartitionLetter(XboxPartitionTableEntryCount - 1), L'N');
}

TEST(FormatHumanSize, ZeroIsMegabytes)
{
    EXPECT_EQ(HumanSize(0), L"0.00 MB");
}

TEST(FormatHumanSize, SmallSizeIsMegabytes)
{
    EXPECT_EQ(HumanSize(750 * SECTORS_PER_MB), L"750.00 MB");
}

TEST(FormatHumanSize, LargestMegabyteValueStaysMegabytes)
{
    EXPECT_EQ(HumanSize(999 * SECTORS_PER_MB + SECTORS_PER_MB / 2), L"999.50 MB");
}

TEST(FormatHumanSize, ValueRoundingTo1000MegabytesMovesToGigabytes)
{
    EXPECT_EQ(HumanSize(1000 * SECTORS_PER_MB - 1), L"0.98 GB");
}

TEST(FormatHumanSize, GigabyteSizeUsesTwoDecimals)
{
    EXPECT_EQ(HumanSize(3 * SECTORS_PER_GB / 2), L"1.50 GB");
}

TEST(FormatHumanSize, TerabyteDriveIsTerabytes)
{
    EXPECT_EQ(HumanSize(2 * SECTORS_PER_TB), L"2.00 TB");
}

TEST(FormatHumanSize, StaysWithinTheSizeColumn)
{
    EXPECT_LE(HumanSize(999 * SECTORS_PER_TB).size(), 9u);
}

TEST(FormatTableLba, ZeroIsTwelveZeros)
{
    EXPECT_EQ(TableLba(0), L"000000000000");
}

TEST(FormatTableLba, SplitsHighAndLowWords)
{
    EXPECT_EQ(TableLba(0x123456789ABCull), L"123456789ABC");
}

TEST(FormatTableLba, KeepsOnly48Bits)
{
    EXPECT_EQ(TableLba(0xFFFF000000000001ull), L"000000000001");
}

TEST(SameTableButVersion, IdenticalTablesMatch)
{
    const XboxPartitionTable table = StockTable();
    EXPECT_TRUE(SameTableButVersion(table, table));
}

TEST(SameTableButVersion, DifferentVersionsMatch)
{
    const XboxPartitionTable table = StockTable();
    XboxPartitionTable titan = table;
    titan.Version = XBOX_TABLE_VERSION_TITAN;
    EXPECT_TRUE(SameTableButVersion(table, titan));
}

TEST(SameTableButVersion, DifferentEntryDoesNotMatch)
{
    const XboxPartitionTable table = StockTable();
    XboxPartitionTable resized = table;
    resized.TableEntries[0].LBASize += 1;
    EXPECT_FALSE(SameTableButVersion(table, resized));
}

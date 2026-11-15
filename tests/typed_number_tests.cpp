// TypedNumber and the number pad's texts: typing, units and the range check

#include <gtest/gtest.h>
#include "typed_number.h"

namespace
{

const uint64_t SECTORS_PER_MB = 0x800;

TypedNumber Typed(int unit, const int *digits, int count)
{
    TypedNumber number(unit);
    for (int i = 0; i < count; i++)
    {
        number.AddDigit(digits[i]);
    }
    return number;
}

TypedNumber Decimal(int value)
{
    const int digits[] = {value / 100 % 10, value / 10 % 10, value % 10};
    return Typed(INPUT_UNIT_SECTORS, digits, 3);
}

std::wstring CurrentValue(uint64_t sectors, int unit)
{
    wchar_t text[INPUT_TEXT_COUNT];
    FormatCurrentValue(sectors, unit, text);
    return text;
}

std::wstring AllowedRange(uint64_t min, uint64_t max, int unit)
{
    wchar_t text[INPUT_TEXT_COUNT];
    FormatAllowedRange(min, max, unit, text);
    return text;
}

} // namespace

TEST(TypedNumber, StartsEmpty)
{
    EXPECT_STREQ(TypedNumber(INPUT_UNIT_GB).Digits(), L"");
}

TEST(TypedNumber, DigitsAreAppended)
{
    EXPECT_STREQ(Decimal(123).Digits(), L"123");
}

TEST(TypedNumber, LeadingZeroIsReplaced)
{
    EXPECT_STREQ(Decimal(7).Digits(), L"7");
}

TEST(TypedNumber, HexDigitsAreLetters)
{
    const int digits[] = {0xA, 0xF};
    EXPECT_STREQ(Typed(INPUT_UNIT_SECTORS_HEX, digits, 2).Digits(), L"AF");
}

TEST(TypedNumber, HexDigitIsIgnoredInDecimal)
{
    const int digits[] = {1, 0xB};
    EXPECT_STREQ(Typed(INPUT_UNIT_MB, digits, 2).Digits(), L"1");
}

TEST(TypedNumber, NoMoreThanTheMaximumDigitsFit)
{
    TypedNumber number(INPUT_UNIT_SECTORS);
    for (int i = 0; i < INPUT_MAX_DIGITS; i++)
    {
        number.AddDigit(9);
    }
    EXPECT_FALSE(number.AddDigit(9));
}

TEST(TypedNumber, DeleteRemovesTheLastDigit)
{
    TypedNumber number = Decimal(123);
    number.DeleteDigit();
    EXPECT_STREQ(number.Digits(), L"12");
}

TEST(TypedNumber, ClearEmptiesIt)
{
    TypedNumber number = Decimal(123);
    number.Clear();
    EXPECT_STREQ(number.Digits(), L"");
}

TEST(TypedNumber, DecimalToHexKeepsTheNumber)
{
    TypedNumber number = Decimal(255);
    number.NextUnit();
    EXPECT_STREQ(number.Digits(), L"FF");
}

TEST(TypedNumber, HexToMegabytesKeepsTheNumber)
{
    const int digits[] = {0xF, 0xF};
    TypedNumber number = Typed(INPUT_UNIT_SECTORS_HEX, digits, 2);
    number.NextUnit();
    EXPECT_STREQ(number.Digits(), L"255");
}

TEST(TypedNumber, SameBaseKeepsTheDigits)
{
    const int digits[] = {4, 2};
    TypedNumber number = Typed(INPUT_UNIT_MB, digits, 2);
    number.NextUnit();
    EXPECT_STREQ(number.Digits(), L"42");
}

TEST(TypedNumber, MegabytesAreFollowedByGigabytes)
{
    TypedNumber number(INPUT_UNIT_MB);
    number.NextUnit();
    EXPECT_EQ(number.Unit(), INPUT_UNIT_GB);
}

TEST(TypedNumber, UnitsWrapAround)
{
    TypedNumber number(INPUT_UNIT_GB);
    number.NextUnit();
    EXPECT_EQ(number.Unit(), INPUT_UNIT_SECTORS);
}

TEST(TypedNumber, EmptyIsNotValid)
{
    EXPECT_STREQ(TypedNumber(INPUT_UNIT_SECTORS).Check(0, 1000).message, L"Type a number first.");
}

TEST(TypedNumber, ValueInRangeIsInSectors)
{
    const int digits[] = {3};
    EXPECT_EQ(Typed(INPUT_UNIT_MB, digits, 1).Check(0, 10 * SECTORS_PER_MB).sectors, 3 * SECTORS_PER_MB);
}

TEST(TypedNumber, ValueOutsideTheRangeSaysTheRange)
{
    EXPECT_STREQ(Decimal(500).Check(10, 100).message, L"Must be from 10 to 100 sectors.");
}

TEST(TypedNumber, RangeWithoutAWholeUnitAsksForASmallerOne)
{
    const int digits[] = {1};
    EXPECT_STREQ(Typed(INPUT_UNIT_GB, digits, 1).Check(1, SECTORS_PER_MB).message, L"Not a whole number of these, use a smaller unit.");
}

TEST(NumberPadTexts, CurrentSectorsInHex)
{
    EXPECT_EQ(CurrentValue(0x4D2, INPUT_UNIT_SECTORS_HEX), L"Now: 0x4D2 sectors");
}

TEST(NumberPadTexts, CurrentMegabytesWithTwoDecimals)
{
    EXPECT_EQ(CurrentValue(3 * SECTORS_PER_MB / 2, INPUT_UNIT_MB), L"Now: 1.50 MB");
}

TEST(NumberPadTexts, AllowedRangeRoundsInward)
{
    EXPECT_EQ(AllowedRange(1, 3 * SECTORS_PER_MB - 1, INPUT_UNIT_MB), L"Can be: 1 to 2 MB");
}

TEST(NumberPadTexts, NoWholeUnitInTheRange)
{
    EXPECT_EQ(AllowedRange(1, SECTORS_PER_MB, INPUT_UNIT_GB), L"Can be: less than one GB, use a smaller unit");
}

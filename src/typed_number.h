#ifndef TYPED_NUMBER_H_
#define TYPED_NUMBER_H_

// A number typed on the number pad, in one of its units, and the texts about
// it. Shared by the Xbox app and the host tests: no XDK headers.

#include "partition.hpp"

// Number pad units, in sectors. Sectors can be typed in decimal or hex.
enum InputUnit
{
    INPUT_UNIT_SECTORS = 0,
    INPUT_UNIT_SECTORS_HEX,
    INPUT_UNIT_MB,
    INPUT_UNIT_GB,
    INPUT_UNIT_COUNT
};

// Times a unit still fits in 64 bits, also in hex (60 bits)
const int INPUT_MAX_DIGITS = 15;
const size_t INPUT_TEXT_COUNT = 100;

uint64_t InputUnitSectors(int unit);
// "sectors (hex)", "MB"
const wchar_t *InputUnitName(int unit);
// 16 for hex sectors, else 10
int InputBase(int unit);

// Whole units within a range of sectors
struct UnitRange
{
    bool any; // False: not even one whole unit fits
    uint64_t min_units;
    uint64_t max_units;
};
UnitRange WholeUnitsWithin(uint64_t min_sectors, uint64_t max_sectors, int unit);

// "Now: 1234 sectors" or "Now: 1.50 GB"; out holds INPUT_TEXT_COUNT characters
void FormatCurrentValue(uint64_t sectors, int unit, wchar_t *out);
// "Can be: 0x0 to 0x400 sectors"; out holds INPUT_TEXT_COUNT characters
void FormatAllowedRange(uint64_t min_sectors, uint64_t max_sectors, int unit, wchar_t *out);

// The typed value, checked against the range
struct TypedValue
{
    bool valid;
    uint64_t sectors;
    wchar_t message[INPUT_TEXT_COUNT]; // Why it isn't valid
};

// The digits typed so far and their unit
class TypedNumber
{
  public:
    explicit TypedNumber(int unit);

    const wchar_t *Digits() const
    {
        return m_digits;
    }
    int Unit() const
    {
        return m_unit;
    }

    // 0-15; a digit the base doesn't have is ignored. False if no more fit.
    bool AddDigit(int digit);
    void DeleteDigit();
    void Clear();
    // The next unit; between decimal and hex the number is kept, in the other base
    void NextUnit();

    // In sectors, if it is within min..max sectors in whole units
    TypedValue Check(uint64_t min_sectors, uint64_t max_sectors) const;

  private:
    uint64_t Value() const;

    wchar_t m_digits[INPUT_MAX_DIGITS + 1];
    int m_length;
    int m_unit;
};

#endif // TYPED_NUMBER_H_

#include "typed_number.h"
#include "partition_format.h"
#include <string.h>

namespace
{

const uint64_t UNIT_SECTORS[INPUT_UNIT_COUNT] = {1, 1, 0x800, 0x200000};
const wchar_t *const UNIT_NAMES[INPUT_UNIT_COUNT] = {L"sectors", L"sectors (hex)", L"MB", L"GB"};
const wchar_t *const UNIT_SHORT_NAMES[INPUT_UNIT_COUNT] = {L"sectors", L"sectors", L"MB", L"GB"};
const size_t NUMBER_TEXT_COUNT = 24;

// A whole number of units as it is typed in that unit: 1234 or 0x4D2
void format_units(uint64_t value, int unit, wchar_t *out)
{
    FormatWide(out, NUMBER_TEXT_COUNT, (InputBase(unit) == 16) ? L"0x%I64X" : L"%I64u", value);
}

} // namespace

uint64_t InputUnitSectors(int unit)
{
    return UNIT_SECTORS[unit];
}

const wchar_t *InputUnitName(int unit)
{
    return UNIT_NAMES[unit];
}

int InputBase(int unit)
{
    return (unit == INPUT_UNIT_SECTORS_HEX) ? 16 : 10;
}

UnitRange WholeUnitsWithin(uint64_t min_sectors, uint64_t max_sectors, int unit)
{
    const uint64_t per_unit = UNIT_SECTORS[unit];
    UnitRange range;
    range.min_units = min_sectors / per_unit + ((min_sectors % per_unit) ? 1 : 0);
    range.max_units = max_sectors / per_unit;
    range.any = range.min_units <= range.max_units;
    return range;
}

void FormatCurrentValue(uint64_t sectors, int unit, wchar_t *out)
{
    if (UNIT_SECTORS[unit] == 1)
    {
        wchar_t amount[NUMBER_TEXT_COUNT];
        format_units(sectors, unit, amount);
        FormatWide(out, INPUT_TEXT_COUNT, L"Now: %s sectors", amount);
    }
    else
    {
        FormatWide(out, INPUT_TEXT_COUNT, L"Now: %.2f %s", (double)sectors / UNIT_SECTORS[unit], UNIT_NAMES[unit]);
    }
}

void FormatAllowedRange(uint64_t min_sectors, uint64_t max_sectors, int unit, wchar_t *out)
{
    const UnitRange range = WholeUnitsWithin(min_sectors, max_sectors, unit);
    if (!range.any)
    {
        FormatWide(out, INPUT_TEXT_COUNT, L"Can be: less than one %s, use a smaller unit", UNIT_SHORT_NAMES[unit]);
        return;
    }
    wchar_t low[NUMBER_TEXT_COUNT];
    wchar_t high[NUMBER_TEXT_COUNT];
    format_units(range.min_units, unit, low);
    format_units(range.max_units, unit, high);
    FormatWide(out, INPUT_TEXT_COUNT, L"Can be: %s to %s %s", low, high, UNIT_SHORT_NAMES[unit]);
}

TypedNumber::TypedNumber(int unit) : m_length(0), m_unit(unit)
{
    m_digits[0] = L'\0';
}

bool TypedNumber::AddDigit(int digit)
{
    if (digit >= InputBase(m_unit))
    {
        return true; // A-F outside hex (not reachable with the D-pad)
    }
    if (m_length == 1 && m_digits[0] == L'0')
    {
        m_length = 0; // No leading zeros
    }
    if (m_length >= INPUT_MAX_DIGITS)
    {
        return false;
    }
    m_digits[m_length++] = (wchar_t)((digit < 10) ? (L'0' + digit) : (L'A' + digit - 10));
    m_digits[m_length] = L'\0';
    return true;
}

void TypedNumber::DeleteDigit()
{
    if (m_length > 0)
    {
        m_digits[--m_length] = L'\0';
    }
}

void TypedNumber::Clear()
{
    m_length = 0;
    m_digits[0] = L'\0';
}

void TypedNumber::NextUnit()
{
    const int old_base = InputBase(m_unit);
    const uint64_t value = Value();
    m_unit = (m_unit + 1) % INPUT_UNIT_COUNT;
    if (InputBase(m_unit) == old_base || m_length == 0)
    {
        return;
    }

    wchar_t converted[NUMBER_TEXT_COUNT];
    FormatWide(converted, NUMBER_TEXT_COUNT, (InputBase(m_unit) == 16) ? L"%I64X" : L"%I64u", value);
    Clear();
    const int length = (int)wcslen(converted);
    if (length <= INPUT_MAX_DIGITS)
    {
        memcpy(m_digits, converted, (length + 1) * sizeof(wchar_t));
        m_length = length;
    }
}

TypedValue TypedNumber::Check(uint64_t min_sectors, uint64_t max_sectors) const
{
    TypedValue result = {false, 0, L""};
    const UnitRange range = WholeUnitsWithin(min_sectors, max_sectors, m_unit);
    if (m_length == 0)
    {
        FormatWide(result.message, INPUT_TEXT_COUNT, L"Type a number first.");
    }
    else if (!range.any)
    {
        FormatWide(result.message, INPUT_TEXT_COUNT, L"Not a whole number of these, use a smaller unit.");
    }
    else if (Value() < range.min_units || Value() > range.max_units)
    {
        wchar_t low[NUMBER_TEXT_COUNT];
        wchar_t high[NUMBER_TEXT_COUNT];
        format_units(range.min_units, m_unit, low);
        format_units(range.max_units, m_unit, high);
        FormatWide(result.message, INPUT_TEXT_COUNT, L"Must be from %s to %s %s.", low, high, UNIT_SHORT_NAMES[m_unit]);
    }
    else
    {
        // value <= max / unit, so this can't overflow
        result.valid = true;
        result.sectors = Value() * UNIT_SECTORS[m_unit];
    }
    return result;
}

uint64_t TypedNumber::Value() const
{
    const int base = InputBase(m_unit);
    uint64_t value = 0;
    for (int i = 0; i < m_length; i++)
    {
        const wchar_t d = m_digits[i];
        value = value * base + ((d >= L'A') ? (d - L'A' + 10) : (d - L'0'));
    }
    return value;
}

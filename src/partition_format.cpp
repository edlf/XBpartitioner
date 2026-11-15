#include "partition_format.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void FormatWide(wchar_t *out, size_t out_count, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
#ifdef _XBOX
    _vsnwprintf(out, out_count, format, args); // The XDK runtime's vswprintf has no size
#else
    vswprintf(out, out_count, format, args);
#endif
    va_end(args);
    out[out_count - 1] = L'\0';
}

namespace
{

const size_t HUMAN_SIZE_MAX_LENGTH = 9; // The table's size column
const double SECTORS_PER_MB = 0x800;
const double SECTORS_PER_GB = 0x200000;
const double SECTORS_PER_TB = 0x80000000;

bool format_size_in_unit(uint64_t sectors, double sectors_per_unit, const wchar_t *unit, wchar_t *out)
{
    FormatWide(out, HUMAN_SIZE_TEXT_COUNT, L"%.2f %s", (double)sectors / sectors_per_unit, unit);
    return wcslen(out) <= HUMAN_SIZE_MAX_LENGTH;
}

} // namespace

wchar_t PartitionLetter(int index)
{
    static const wchar_t stock_letters[XBOX_STANDARD_PARTITION_COUNT] = {L'E', L'C', L'X', L'Y', L'Z'};

    if (index < XBOX_STANDARD_PARTITION_COUNT)
    {
        return stock_letters[index];
    }
    return (wchar_t)(L'F' + (index - XBOX_STANDARD_PARTITION_COUNT));
}

// Checked on the text, so a value rounding up to 1000.00 moves to the next unit
void FormatHumanSize(uint64_t sectors, wchar_t *out)
{
    if (format_size_in_unit(sectors, SECTORS_PER_MB, L"MB", out))
    {
        return;
    }
    if (format_size_in_unit(sectors, SECTORS_PER_GB, L"GB", out))
    {
        return;
    }
    format_size_in_unit(sectors, SECTORS_PER_TB, L"TB", out);
}

void FormatTableLba(uint64_t lba, wchar_t *out)
{
    FormatWide(out, TABLE_LBA_TEXT_COUNT, L"%.04X%.08X", (unsigned int)(uint16_t)(lba >> 32), (unsigned int)(lba & LBA_LOWER_BITS));
}

void FormatPartitionList(const PartitionSet &partitions, wchar_t *out, size_t out_count)
{
    out[0] = L'\0';
    size_t used = 0;

    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (!partitions.Contains(i))
        {
            continue;
        }

        wchar_t item[16];
        FormatWide(item, 16, L"%s%d (%c)", used ? L", " : L"", i + 1, PartitionLetter(i));

        const size_t item_length = wcslen(item);
        if (used + item_length + 1 > out_count)
        {
            break;
        }
        memcpy(out + used, item, (item_length + 1) * sizeof(wchar_t));
        used += item_length;
    }
}

bool SameTableButVersion(const XboxPartitionTable &a, const XboxPartitionTable &b)
{
    XboxPartitionTable x = a;
    XboxPartitionTable y = b;
    x.Version = y.Version = 0;
    return memcmp(&x, &y, sizeof(XboxPartitionTable)) == 0;
}

// Small helpers shared by the screens.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// Fraction of the remaining distance a selection bar moves per 1/60 s
static const FLOAT SELECTION_SLIDE_FACTOR = 0.35f;
static const FLOAT SLIDE_FRAME_MS = 1000.0f / 60.0f;
// After a pause (another screen) the bar continues instead of jumping
static const DWORD SLIDE_MAX_STEP_MS = 50;

void SlideTowards(FLOAT &position, FLOAT target, DWORD &last_tick)
{
    // By time: slow frames would otherwise make the bar crawl
    const DWORD now = GetTickCount();
    DWORD elapsed = (last_tick == 0) ? (DWORD)SLIDE_FRAME_MS : now - last_tick;
    last_tick = now;
    if (elapsed > SLIDE_MAX_STEP_MS)
    {
        elapsed = SLIDE_MAX_STEP_MS;
    }

    const FLOAT remaining = target - position;
    if (remaining > -0.5f && remaining < 0.5f)
    {
        position = target;
        return;
    }

    // (1 - factor) of the distance is left after each 1/60 s
    const FLOAT left = (FLOAT)pow(1.0 - SELECTION_SLIDE_FACTOR, (double)elapsed / SLIDE_FRAME_MS);
    position = target - remaining * left;
}

wchar_t partition_letter(int index)
{
    static const wchar_t stock_letters[XBOX_STANDARD_PARTITION_COUNT] = {L'E', L'C', L'X', L'Y', L'Z'};

    if (index < XBOX_STANDARD_PARTITION_COUNT)
    {
        return stock_letters[index];
    }
    return (wchar_t)(L'F' + (index - XBOX_STANDARD_PARTITION_COUNT));
}

// At most 9 characters (the table's size column). Checked on the text, so
// a value rounding up to 1000.00 moves to the next unit.
void format_human_size(ULONGLONG sectors, wchar_t *out)
{
    const int max_length = 9;

    swprintf(out, L"%.2f MB", (double)sectors / 0x800);
    if (wcslen(out) <= max_length)
    {
        return;
    }

    swprintf(out, L"%.2f GB", (double)sectors / 0x200000);
    if (wcslen(out) <= max_length)
    {
        return;
    }

    swprintf(out, L"%.2f TB", (double)sectors / 0x80000000);
}

void format_table_lba(ULONGLONG lba, wchar_t *out)
{
    swprintf(out, L"%.04X%.08X", (ULONG)(WORD)(lba >> 32), (ULONG)(lba & LBA_LOWER_BITS));
}

bool same_table_but_version(const XboxPartitionTable &a, const XboxPartitionTable &b)
{
    XboxPartitionTable x = a;
    XboxPartitionTable y = b;
    x.Version = y.Version = 0;
    return memcmp(&x, &y, sizeof(XboxPartitionTable)) == 0;
}

#ifndef APP_INTERNAL_H_
#define APP_INTERNAL_H_

// Shared by XBpartitioner.cpp and the app_*.cpp files only.

#include <stdio.h>  // swprintf
#include <stdlib.h>
#include <string.h> // wcscat, memset
#include <math.h>   // pow, for the selection bars

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// Drive letter of a partition table index: E, C, X, Y, Z, then F, G, ...
wchar_t partition_letter(int index);
// Sector count as "750.00 MB", "1.50 GB" or "1.82 TB", at most 9 characters
void format_human_size(ULONGLONG sectors, wchar_t *out);
// A start or end sector, 48 bits as 12 hex digits
void format_table_lba(ULONGLONG lba, wchar_t *out);

// Number pad units, in sectors. Sectors can be typed in decimal or hex.
enum InputUnit
{
    INPUT_UNIT_SECTORS = 0,
    INPUT_UNIT_SECTORS_HEX,
    INPUT_UNIT_MB,
    INPUT_UNIT_GB,
    INPUT_UNIT_COUNT
};

#endif // APP_INTERNAL_H_

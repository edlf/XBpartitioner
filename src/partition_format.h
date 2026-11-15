#ifndef PARTITION_FORMAT_H_
#define PARTITION_FORMAT_H_

// Text and comparisons for partition tables, shared by the Xbox app and the
// host tests: no XDK headers.

#include "partition.hpp"
#include "partition_set.h"

// Buffer sizes, in characters, for the formatters below
const size_t HUMAN_SIZE_TEXT_COUNT = 32;
const size_t TABLE_LBA_TEXT_COUNT = 16;

// swprintf with the buffer size on both the XDK runtime and the PC's; always
// terminated, cut to fit out_count characters
void FormatWide(wchar_t *out, size_t out_count, const wchar_t *format, ...);
// Drive letter of a partition table index: E, C, X, Y, Z, then F, G, ...
wchar_t PartitionLetter(int index);
// Sector count as "750.00 MB", "1.50 GB" or "1.82 TB", at most 9 characters
// below 1000 TB. out holds HUMAN_SIZE_TEXT_COUNT characters.
void FormatHumanSize(uint64_t sectors, wchar_t *out);
// A start or end sector, 48 bits as 12 hex digits. out holds
// TABLE_LBA_TEXT_COUNT characters.
void FormatTableLba(uint64_t lba, wchar_t *out);
// "1 (E), 2 (C), 6 (F)": the partitions in the set, as many as fit in
// out_count characters
void FormatPartitionList(const PartitionSet &partitions, wchar_t *out, size_t out_count);
// Equal apart from the version: with a Titan BIOS the backup is 0x03 and a
// copy in sector 0 is v4 or v3
bool SameTableButVersion(const XboxPartitionTable &a, const XboxPartitionTable &b);

#endif // PARTITION_FORMAT_H_

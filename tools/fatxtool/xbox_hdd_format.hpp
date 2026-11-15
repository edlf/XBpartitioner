#ifndef XBOX_HDD_FORMAT_H_
#define XBOX_HDD_FORMAT_H_

// Formats a whole drive as an Xbox hard drive on the PC: an Xbox partition
// table (XBpartitioner's presets) and a FATX file system on every partition

#include "fatx_check.hpp"

// XBpartitioner's presets
enum XboxHddLayout
{
    XBOX_HDD_STOCK = 0, // E, C, X, Y, Z only
    XBOX_HDD_F,         // F takes the rest of the drive
    XBOX_HDD_F_G,       // F and G split it
    XBOX_HDD_LAYOUT_COUNT
};

struct XboxHddSpec
{
    XboxHddLayout layout;
    bool lba48_v4; // 48 bit table (over 2 TB, Cerbios); false: v3, partitions up to 1 TB
    uint32_t serial; // The first partition's volume serial; the others count up
};

// The smallest drive the stock layout fits on
const uint64_t XBOX_HDD_MIN_BYTES = (uint64_t)XBOX_STANDARD_MAX_LBA * HDD_SECTOR_SIZE;

// The partition table for a drive of this many sectors. Laid out as for a
// 512e drive: the backup table goes in the last 4K block, clear of every
// partition, which suits drives with 512 byte sectors too.
PartitionTableEditor xbox_hdd_table(uint64_t sectors, const XboxHddSpec &spec);

// Erases the drive's old table, formats every partition, then writes the
// backup tables and sector 0, so an interrupted format never looks like a
// usable Xbox drive. False if the drive is too small or a write failed.
bool format_xbox_hdd(FatxDevice &disk, uint64_t disk_bytes, const XboxHddSpec &spec, FatxProgress *progress = NULL);

#endif // XBOX_HDD_FORMAT_H_

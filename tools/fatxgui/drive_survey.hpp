#ifndef FATXGUI_DRIVE_SURVEY_HPP_
#define FATXGUI_DRIVE_SURVEY_HPP_

// What's on each of the PC's physical drives, read only: an Xbox partition
// table, a FATX volume straight on the drive (like a memory unit), or
// something else, and which volumes Windows has mounted on it

#include <stdint.h>
#include <string>
#include <vector>

namespace fatxgui
{

enum DriveContent
{
    DRIVE_XBOX_TABLE, // Xbox partition table in sector 0
    DRIVE_FATX,       // FATX from sector 0, no table
    DRIVE_OTHER
};

struct DriveInfo
{
    int number = -1; // PhysicalDriveN
    uint64_t bytes = 0;
    std::string model;
    bool usb = false;       // Connected by USB
    bool removable = false; // Says its media is removable, as USB sticks and card readers do
    DriveContent content = DRIVE_OTHER;
    int partitions = 0;                   // With an Xbox table: the ones in use
    bool windows_partition_table = false; // MBR or GPT
    bool data_at_start = false;           // Bytes other than 0x00 and 0xFF in its first 4 MB
    // Volumes Windows has mounted on it, with or without a drive letter:
    // "C:, D:\Mount, a volume without a drive letter (NTFS)"; empty if none
    std::string mounted_volumes;
};

// False if there is no such drive or it can't be read (no administrator rights)
bool survey_drive(int number, DriveInfo *info);

// The volumes Windows has mounted on PhysicalDriveN, as DriveInfo lists them;
// needs no administrator rights
std::string mounted_volumes_on(int number);

// How much of a drive's start is looked at for data an Xbox format may lose
const uint64_t DRIVE_START_BYTES = 4ULL * 1024 * 1024;

// Clean: nothing but 0x00 and 0xFF bytes
bool is_blank(const unsigned char *data, size_t length);

// The biggest drive that may be formatted as FATX
const uint64_t FORMAT_MAX_BYTES = 4ULL * 1024 * 1024 * 1024;

// Why it may not be formatted as FATX, or empty if it may: only removable USB
// drives up to 4 GB (sticks and cards, like a memory unit), never one with a
// mounted volume
std::string format_refusal(const DriveInfo &drive);
bool can_format(const DriveInfo &drive);

// Why it may not be formatted as an Xbox hard drive, or empty if it may:
// never one with a mounted volume, and big enough for the stock layout
std::string xbox_format_refusal(const DriveInfo &drive);

// "PhysicalDrive3 (Kingston DataTraveler, 14.92 GB)"
std::string drive_label(const DriveInfo &drive);
// What formatting it erases, a line each, for the confirmation
std::vector<std::string> drive_loss_lines(const DriveInfo &drive);

// Asks Windows to read the drive's layout again, after it was formatted
void refresh_drive_layout(int number);

} // namespace fatxgui

#endif // FATXGUI_DRIVE_SURVEY_HPP_

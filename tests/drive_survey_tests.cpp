// fatxgui's drive survey: which drives may be formatted as FATX

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>
#include "drive_survey.hpp"
#include "fatx_check.hpp"

using namespace fatxgui;

namespace
{

const uint64_t GIGABYTE = 1024ULL * 1024 * 1024;

// A 2 GB USB stick with nothing Windows mounts
DriveInfo UsbStick()
{
    DriveInfo drive;
    drive.number = 5;
    drive.bytes = 2 * GIGABYTE;
    drive.usb = true;
    drive.removable = true;
    return drive;
}

} // namespace

TEST(CanFormat, ARemovableUsbDriveUpTo4Gb)
{
    EXPECT_TRUE(can_format(UsbStick()));
}

TEST(CanFormat, ExactlyFourGigabytes)
{
    DriveInfo drive = UsbStick();
    drive.bytes = 4 * GIGABYTE;
    EXPECT_TRUE(can_format(drive));
}

TEST(CanFormat, NotOverFourGigabytes)
{
    DriveInfo drive = UsbStick();
    drive.bytes = 4 * GIGABYTE + HDD_SECTOR_SIZE;
    EXPECT_FALSE(can_format(drive));
}

TEST(CanFormat, NotADriveOtherThanUsb)
{
    DriveInfo drive = UsbStick();
    drive.usb = false;
    EXPECT_FALSE(can_format(drive));
}

TEST(CanFormat, NotAUsbDriveThatIsNotRemovable)
{
    // USB hard drives say their media is fixed
    DriveInfo drive = UsbStick();
    drive.removable = false;
    EXPECT_FALSE(can_format(drive));
}

TEST(CanFormat, NeverADriveWithAWindowsVolume)
{
    DriveInfo drive = UsbStick();
    drive.mounted_volumes = "E:";
    EXPECT_FALSE(can_format(drive));
}

TEST(CanFormat, ADriveWithAWindowsPartitionTableButNoVolume)
{
    // Its partitions have no drive letter: the confirmation says they go
    DriveInfo drive = UsbStick();
    drive.windows_partition_table = true;
    EXPECT_TRUE(can_format(drive));
}

TEST(CanFormat, NotADriveTooSmallForFatx)
{
    DriveInfo drive = UsbStick();
    drive.bytes = FATX_HEADER_BYTES * 4;
    EXPECT_FALSE(can_format(drive));
}

TEST(FormatRefusal, SaysWhy)
{
    DriveInfo drive = UsbStick();
    drive.bytes = 8 * GIGABYTE;
    EXPECT_EQ(format_refusal(drive), "Only drives up to 4 GB are formatted.");
}

TEST(SurveyDrive, FalseForADriveThatIsNotThere)
{
    DriveInfo drive;
    EXPECT_FALSE(survey_drive(9999, &drive));
}

namespace
{

// The PhysicalDrive number of the disk Windows runs from, or -1
int WindowsDiskNumber()
{
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const wchar_t path[] = {L'\\', L'\\', L'.', L'\\', windows[0], L':', L'\0'};
    HANDLE volume = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    DWORD number[3] = {0, 0, 0}; // STORAGE_DEVICE_NUMBER: type, device, partition
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(volume, 0x002D1080 /* IOCTL_STORAGE_GET_DEVICE_NUMBER */, NULL, 0, number, sizeof(number), &returned, NULL);
    CloseHandle(volume);
    return ok ? (int)number[1] : -1;
}

} // namespace

TEST(IsBlank, ZerosAndOnesMixed)
{
    const unsigned char bytes[] = {0x00, 0xFF, 0xFF, 0x00};
    EXPECT_TRUE(is_blank(bytes, sizeof(bytes)));
}

TEST(IsBlank, NotWithAnyOtherByte)
{
    const unsigned char bytes[] = {0x00, 0xFF, 0x55, 0x00};
    EXPECT_FALSE(is_blank(bytes, sizeof(bytes)));
}

TEST(MountedVolumes, FindsTheWindowsDrive)
{
    const int disk = WindowsDiskNumber();
    ASSERT_GE(disk, 0);
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::string letter = std::string(1, (char)windows[0]) + ":";
    EXPECT_NE(mounted_volumes_on(disk).find(letter), std::string::npos) << mounted_volumes_on(disk);
}

TEST(MountedVolumes, NoneOnADriveThatIsNotThere)
{
    EXPECT_EQ(mounted_volumes_on(9999), "");
}

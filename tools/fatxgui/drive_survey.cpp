// See drive_survey.hpp

// Lean, because the full windows.h pulls in <winioctl.h>, which would pick
// up the Xbox version in src\. The definitions needed are below.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "drive_survey.hpp"

#include <string.h>
#include <vector>

#include "fatx_resize.hpp" // FATX_RESIZING_SIGNATURE
#include "fatx_trim.hpp"   // WindowsDisk
#include "xbox_hdd_format.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

const DWORD IOCTL_STORAGE_QUERY_PROPERTY_ = 0x002D1400;
const DWORD IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS_ = 0x00560000;
const DWORD IOCTL_DISK_UPDATE_PROPERTIES_ = 0x00070140;

// STORAGE_DEVICE_DESCRIPTOR: where the fields used here are
const size_t DESCRIPTOR_REMOVABLE_OFFSET = 10;
const size_t DESCRIPTOR_VENDOR_OFFSET = 12;
const size_t DESCRIPTOR_PRODUCT_OFFSET = 16;
const size_t DESCRIPTOR_BUS_OFFSET = 28;
const DWORD BUS_TYPE_USB = 7; // BusTypeUsb
// VOLUME_DISK_EXTENTS: the count, then 24 byte DISK_EXTENTs from offset 8,
// each starting with its disk number
const size_t EXTENTS_FIRST = 8;
const size_t EXTENT_BYTES = 24;
const size_t MBR_SIGNATURE_OFFSET = 510;

std::string drive_path(int number)
{
    return format("\\\\.\\PhysicalDrive%d", number);
}

// For queries only: no read or write access needed
HANDLE open_for_queries(const std::string &path)
{
    return CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

std::string trimmed(std::string text)
{
    while (!text.empty() && text.back() == ' ')
    {
        text.pop_back();
    }
    const size_t first = text.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : text.substr(first);
}

std::string text_at(const std::vector<unsigned char> &buffer, DWORD offset)
{
    if (offset == 0 || offset >= buffer.size())
    {
        return std::string();
    }
    const char *text = (const char *)buffer.data() + offset;
    return trimmed(std::string(text, strnlen(text, buffer.size() - offset)));
}

// What the drive says about itself: "Kingston DataTraveler 3.0", its bus and
// whether its media is removable
void describe_drive(int number, DriveInfo *info)
{
    HANDLE handle = open_for_queries(drive_path(number));
    if (handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD query[3] = {0, 0, 0}; // StorageDeviceProperty, PropertyStandardQuery
    std::vector<unsigned char> descriptor(1024, 0);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY_, query, sizeof(query), descriptor.data(), (DWORD)descriptor.size(), &returned, NULL);
    CloseHandle(handle);
    if (!ok || returned < DESCRIPTOR_BUS_OFFSET + 4)
    {
        return;
    }
    DWORD vendor_offset, product_offset, bus;
    memcpy(&vendor_offset, descriptor.data() + DESCRIPTOR_VENDOR_OFFSET, 4);
    memcpy(&product_offset, descriptor.data() + DESCRIPTOR_PRODUCT_OFFSET, 4);
    memcpy(&bus, descriptor.data() + DESCRIPTOR_BUS_OFFSET, 4);
    const std::string vendor = text_at(descriptor, vendor_offset);
    const std::string product = text_at(descriptor, product_offset);
    info->model = trimmed(vendor + (vendor.empty() ? "" : " ") + product);
    info->usb = bus == BUS_TYPE_USB;
    info->removable = descriptor[DESCRIPTOR_REMOVABLE_OFFSET] != 0;
}

// Whether a volume ("\\?\Volume{...}\") has any part on the disk
bool volume_is_on_disk(const std::wstring &volume, int number)
{
    // Opened without the trailing backslash: the volume, not its root folder
    const std::wstring device = volume.substr(0, volume.size() - 1);
    HANDLE handle = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    std::vector<unsigned char> extents(EXTENTS_FIRST + 32 * EXTENT_BYTES, 0);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS_, NULL, 0, extents.data(), (DWORD)extents.size(), &returned, NULL);
    CloseHandle(handle);
    if (!ok)
    {
        return false; // Not on a disk (optical, RAM disk)
    }
    DWORD count;
    memcpy(&count, extents.data(), 4);
    for (DWORD i = 0; i < count && EXTENTS_FIRST + (i + 1) * EXTENT_BYTES <= extents.size(); i++)
    {
        DWORD disk;
        memcpy(&disk, extents.data() + EXTENTS_FIRST + i * EXTENT_BYTES, 4);
        if ((int)disk == number)
        {
            return true;
        }
    }
    return false;
}

// Where a volume is mounted ("C:", "D:\Mount"), or "a volume without a
// drive letter (NTFS)"; empty if Windows hasn't mounted a file system on it
std::string mounted_as(const std::wstring &volume)
{
    std::vector<wchar_t> paths(4096, L'\0');
    DWORD length = 0;
    std::string places;
    if (GetVolumePathNamesForVolumeNameW(volume.c_str(), paths.data(), (DWORD)paths.size(), &length))
    {
        for (const wchar_t *path = paths.data(); *path != L'\0'; path += wcslen(path) + 1)
        {
            std::wstring place = path;
            if (place.size() > 1 && place.back() == L'\\')
            {
                place.pop_back(); // "C:\" shows as "C:"
            }
            places += (places.empty() ? "" : ", ") + utf8(place);
        }
    }
    if (!places.empty())
    {
        return places;
    }
    wchar_t file_system[MAX_PATH + 1] = L"";
    if (GetVolumeInformationW(volume.c_str(), NULL, 0, NULL, NULL, NULL, file_system, MAX_PATH + 1))
    {
        return "a volume without a drive letter (" + utf8(file_system) + ")";
    }
    return std::string();
}

} // namespace

bool is_blank(const unsigned char *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        if (data[i] != 0x00 && data[i] != 0xFF)
        {
            return false;
        }
    }
    return true;
}

std::string mounted_volumes_on(int number)
{
    std::string mounted;
    wchar_t volume[MAX_PATH];
    HANDLE search = FindFirstVolumeW(volume, MAX_PATH);
    if (search == INVALID_HANDLE_VALUE)
    {
        return mounted;
    }
    // An empty card reader must not pop up "insert a disk"
    const UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    do
    {
        if (volume_is_on_disk(volume, number))
        {
            const std::string place = mounted_as(volume);
            mounted += place.empty() ? "" : (mounted.empty() ? "" : ", ") + place;
        }
    } while (FindNextVolumeW(search, volume, MAX_PATH));
    FindVolumeClose(search);
    SetErrorMode(old_mode);
    return mounted;
}

namespace
{

// What sector 0 holds
void read_content(WindowsDisk &disk, DriveInfo *info)
{
    unsigned char sector[HDD_SECTOR_SIZE];
    if (!disk.Read(0, sector, sizeof(sector)))
    {
        return;
    }
    XboxPartitionTable table;
    memcpy(&table, sector, sizeof(table));
    uint32_t signature;
    memcpy(&signature, sector, 4);
    if (PartitionTableEditor::HasMagic(table))
    {
        info->content = DRIVE_XBOX_TABLE;
        const PartitionTableEditor editor(table, info->bytes / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
        info->partitions = editor.GetActivePartitionCount();
    }
    else if (signature == FATX_signature || signature == FATX_RESIZING_SIGNATURE)
    {
        info->content = DRIVE_FATX;
    }
    info->windows_partition_table = sector[MBR_SIGNATURE_OFFSET] == 0x55 && sector[MBR_SIGNATURE_OFFSET + 1] == 0xAA;
}

// Anything but 0x00 and 0xFF in the first 4 MB (or what can't be read)
bool has_data_at_start(WindowsDisk &disk, uint64_t bytes)
{
    const uint64_t length = bytes < DRIVE_START_BYTES ? bytes : DRIVE_START_BYTES;
    std::vector<unsigned char> chunk(1024 * 1024);
    for (uint64_t offset = 0; offset < length; offset += chunk.size())
    {
        const uint32_t part = (uint32_t)((length - offset < chunk.size()) ? length - offset : chunk.size());
        if (!disk.Read(offset, chunk.data(), part) || !is_blank(chunk.data(), part))
        {
            return true;
        }
    }
    return false;
}

} // namespace

bool survey_drive(int number, DriveInfo *info)
{
    WindowsDisk disk;
    unsigned long error = 0;
    if (!disk.Open(drive_path(number).c_str(), false, &error))
    {
        return false;
    }
    *info = DriveInfo();
    info->number = number;
    info->bytes = disk.Size();
    describe_drive(number, info);
    read_content(disk, info);
    info->data_at_start = has_data_at_start(disk, info->bytes);
    info->mounted_volumes = mounted_volumes_on(number);
    return true;
}

std::string format_refusal(const DriveInfo &drive)
{
    if (!drive.mounted_volumes.empty())
    {
        return "Windows has volumes on it mounted (" + drive.mounted_volumes + "): unmount or remove them first.";
    }
    if (!drive.usb || !drive.removable)
    {
        return "Only removable USB drives (sticks and card readers) are formatted.";
    }
    if (drive.bytes > FORMAT_MAX_BYTES)
    {
        return "Only drives up to 4 GB are formatted.";
    }
    if (drive.bytes < FATX_HEADER_BYTES * 16)
    {
        return "It's too small for FATX.";
    }
    return std::string();
}

bool can_format(const DriveInfo &drive)
{
    return format_refusal(drive).empty();
}

std::string xbox_format_refusal(const DriveInfo &drive)
{
    if (!drive.mounted_volumes.empty())
    {
        return "Windows has volumes on it mounted (" + drive.mounted_volumes + "): unmount or remove them first.";
    }
    if (drive.bytes < XBOX_HDD_MIN_BYTES)
    {
        return "It's smaller than the stock Xbox layout (" + size_text(XBOX_HDD_MIN_BYTES) + ").";
    }
    return std::string();
}

std::string drive_label(const DriveInfo &drive)
{
    return format("PhysicalDrive%d (", drive.number) + (drive.model.empty() ? "" : drive.model + ", ") + size_text(drive.bytes) + ")";
}

std::vector<std::string> drive_loss_lines(const DriveInfo &drive)
{
    std::vector<std::string> lines = {"Everything on " + drive_label(drive) + " is erased."};
    if (drive.content == DRIVE_XBOX_TABLE)
    {
        lines.push_back(format("It holds an Xbox partition table with %d partitions: all of them are lost.", drive.partitions));
    }
    else if (drive.content == DRIVE_FATX)
    {
        lines.push_back("It holds a FATX volume: its files are lost.");
    }
    if (drive.windows_partition_table)
    {
        lines.push_back("Its Windows partition table goes too, with any partitions it lists.");
    }
    return lines;
}

void refresh_drive_layout(int number)
{
    HANDLE handle = CreateFileA(drive_path(number).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD returned = 0;
    DeviceIoControl(handle, IOCTL_DISK_UPDATE_PROPERTIES_, NULL, 0, NULL, 0, &returned, NULL);
    CloseHandle(handle);
}

} // namespace fatxgui

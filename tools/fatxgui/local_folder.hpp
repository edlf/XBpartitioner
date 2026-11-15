#ifndef FATXGUI_LOCAL_FOLDER_HPP_
#define FATXGUI_LOCAL_FOLDER_HPP_

// The PC's drives and folders, for the browser's left pane

#include <windows.h>

#include <stdint.h>
#include <string>
#include <vector>

namespace fatxgui
{

struct LocalEntry
{
    std::wstring name;
    bool directory;
    uint64_t size;
    FILETIME modified;
};

struct LocalDrive
{
    std::wstring root; // "C:\"
    std::wstring label;
    UINT type;         // DRIVE_FIXED, DRIVE_REMOVABLE...
    bool ready;        // False for an empty card reader or DVD drive
    uint64_t total_bytes;
    uint64_t free_bytes;
};

// Folders first, then by name, ignoring case. False, with the reason, if the
// folder can't be read.
bool list_local_folder(const std::wstring &folder, std::vector<LocalEntry> *entries, std::string *error);
std::vector<LocalDrive> list_local_drives();

// "2026-11-15 13:05", in local time; "" if unset
std::string local_time_text(const FILETIME &time);
// "C:  System  (fixed, 120.50 GB free of 931.51 GB)"
std::string local_drive_text(const LocalDrive &drive);

} // namespace fatxgui

#endif // FATXGUI_LOCAL_FOLDER_HPP_

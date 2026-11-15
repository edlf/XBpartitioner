// See local_folder.hpp

#define NOMINMAX
#include "local_folder.hpp"

#include <algorithm>
#include <stdio.h>

#include "text.hpp"

namespace fatxgui
{

namespace
{

bool listed_before(const LocalEntry &a, const LocalEntry &b)
{
    if (a.directory != b.directory)
    {
        return a.directory;
    }
    return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
}

bool is_dot_entry(const wchar_t *name)
{
    return wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
}

std::wstring with_trailing_slash(const std::wstring &folder)
{
    return (!folder.empty() && folder.back() != L'\\') ? folder + L"\\" : folder;
}

const char *drive_type_name(UINT type)
{
    switch (type)
    {
    case DRIVE_FIXED:
        return "fixed";
    case DRIVE_REMOVABLE:
        return "removable";
    case DRIVE_REMOTE:
        return "network";
    case DRIVE_CDROM:
        return "optical";
    case DRIVE_RAMDISK:
        return "RAM disk";
    default:
        return "drive";
    }
}

} // namespace

bool list_local_folder(const std::wstring &folder, std::vector<LocalEntry> *entries, std::string *error)
{
    entries->clear();
    WIN32_FIND_DATAW found;
    const std::wstring pattern = with_trailing_slash(folder) + L"*";
    HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND)
        {
            return true; // Empty, e.g. a drive's root with nothing on it
        }
        *error = code == ERROR_ACCESS_DENIED ? "Access denied." : format("Can't read it (error %lu).", code);
        return false;
    }
    do
    {
        if (is_dot_entry(found.cFileName))
        {
            continue;
        }
        LocalEntry entry;
        entry.name = found.cFileName;
        entry.directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.size = entry.directory ? 0 : ((uint64_t)found.nFileSizeHigh << 32 | found.nFileSizeLow);
        entry.modified = found.ftLastWriteTime;
        entries->push_back(entry);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(entries->begin(), entries->end(), listed_before);
    return true;
}

std::vector<LocalDrive> list_local_drives()
{
    std::vector<LocalDrive> drives;
    wchar_t roots[512];
    const DWORD length = GetLogicalDriveStringsW((DWORD)(sizeof(roots) / sizeof(roots[0])), roots);
    if (length == 0 || length > sizeof(roots) / sizeof(roots[0]))
    {
        return drives;
    }
    // An empty card reader or DVD drive must not pop up "insert a disk"
    const UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    for (const wchar_t *root = roots; *root != L'\0'; root += wcslen(root) + 1)
    {
        LocalDrive drive;
        drive.root = root;
        drive.type = GetDriveTypeW(root);
        wchar_t label[MAX_PATH + 1] = L"";
        drive.ready = GetVolumeInformationW(root, label, MAX_PATH + 1, NULL, NULL, NULL, NULL, 0) != FALSE;
        drive.label = label;
        ULARGE_INTEGER free_bytes = {}, total_bytes = {};
        if (!drive.ready || !GetDiskFreeSpaceExW(root, &free_bytes, &total_bytes, NULL))
        {
            free_bytes.QuadPart = total_bytes.QuadPart = 0;
        }
        drive.free_bytes = free_bytes.QuadPart;
        drive.total_bytes = total_bytes.QuadPart;
        drives.push_back(drive);
    }
    SetErrorMode(old_mode);
    return drives;
}

std::string local_time_text(const FILETIME &time)
{
    FILETIME local;
    SYSTEMTIME parts;
    const bool unset = time.dwLowDateTime == 0 && time.dwHighDateTime == 0;
    if (unset || !FileTimeToLocalFileTime(&time, &local) || !FileTimeToSystemTime(&local, &parts))
    {
        return std::string();
    }
    return format("%04u-%02u-%02u %02u:%02u", parts.wYear, parts.wMonth, parts.wDay, parts.wHour, parts.wMinute);
}

std::string local_drive_text(const LocalDrive &drive)
{
    const std::string name = utf8(drive.root.substr(0, 2)) + (drive.label.empty() ? "" : "  " + utf8(drive.label));
    if (!drive.ready)
    {
        return name + format("  (%s, no disk)", drive_type_name(drive.type));
    }
    return name + format("  (%s, %s free of %s)", drive_type_name(drive.type), size_text(drive.free_bytes).c_str(), size_text(drive.total_bytes).c_str());
}

} // namespace fatxgui

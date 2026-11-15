#define NOMINMAX
#include "windows_shell.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace fatxgui
{

bool pick_file(HWND owner, bool save, std::wstring *path)
{
    wchar_t buffer[MAX_PATH * 4] = L"";
    if (!path->empty())
    {
        wcsncpy_s(buffer, path->c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Images (*.img, *.bin, *.raw)\0*.img;*.bin;*.raw\0All files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = ARRAYSIZE(buffer);
    ofn.lpstrDefExt = save ? L"img" : nullptr;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok)
    {
        *path = buffer;
    }
    return ok != FALSE;
}

bool pick_folder(HWND owner, std::wstring *path)
{
    IFileOpenDialog *dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        return false;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    bool ok = false;
    if (SUCCEEDED(dialog->Show(owner)))
    {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
            {
                *path = name;
                CoTaskMemFree(name);
                ok = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return ok;
}

bool is_elevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

void restart_elevated(HWND window)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if ((INT_PTR)ShellExecuteW(nullptr, L"runas", exe, nullptr, nullptr, SW_SHOWNORMAL) > 32)
    {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
}

} // namespace fatxgui

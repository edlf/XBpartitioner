#ifndef FATXGUI_WINDOWS_SHELL_HPP_
#define FATXGUI_WINDOWS_SHELL_HPP_

// Windows dialogs for files and folders, and running as administrator

#include <windows.h>

#include <string>

namespace fatxgui
{

// path: the suggested one, then the chosen one. False if cancelled.
bool pick_file(HWND owner, bool save, std::wstring *path);
bool pick_folder(HWND owner, std::wstring *path);

bool is_elevated();
// Starts it again as administrator, and closes this window if that worked
void restart_elevated(HWND window);

} // namespace fatxgui

#endif // FATXGUI_WINDOWS_SHELL_HPP_

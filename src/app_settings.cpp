// The settings file (xbpartitioner.ini next to the xbe). What it holds and
// its text are in src/settings.cpp.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

static const char SETTINGS_PATH[] = "D:\\xbpartitioner.ini";

// Missing or unreadable: the defaults stay
void App::LoadSettings()
{
    HANDLE file = CreateFile(SETTINGS_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    char text[512];
    DWORD read = 0;
    const BOOL ok = ReadFile(file, text, sizeof(text) - 1, &read, NULL);
    CloseHandle(file);
    if (!ok)
    {
        return;
    }
    text[read] = '\0';
    m_settings.Parse(text);

    const int theme = Renderer::FindTheme(m_settings.ThemeName());
    if (theme >= 0)
    {
        m_ui.SetTheme(theme);
    }
}

// Best effort and silent (DVD, or D:'s partition changed)
void App::SaveSettings()
{
    if (m_writer.SettingsSaveBlocked())
    {
        return;
    }

    m_settings.SetThemeName(m_ui.GetTheme().name);
    char text[200];
    const size_t length = m_settings.Format(text, sizeof(text));

    // The drive can take a while (spinning up): without this the screen just stops
    static const LPCWSTR busy = L"Saving settings...";
    const DWORD opened = m_dialogs.OpenBusyWindow(busy);

    HANDLE file = CreateFile(SETTINGS_PATH, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, text, (DWORD)length, &written, NULL);
        CloseHandle(file);
    }

    m_dialogs.CloseBusyWindow(busy, opened);
}

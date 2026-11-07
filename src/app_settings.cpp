// The settings file (xbpartitioner.ini next to the xbe).

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// One key=value per line, keys in any case, spaces ignored; missing or
// unknown values mean the default (see the README):
//   theme=Name, animations=0/1, force_lba48_v3=0/1
static const char SETTINGS_PATH[] = "D:\\xbpartitioner.ini";

static const char SETTING_THEME[] = "theme";

static const char SETTING_ANIMATIONS[] = "animations";

static const char SETTING_FORCE_LBA48_V3[] = "force_lba48_v3";

static char *trim_setting(char *text)
{
    while (*text == ' ' || *text == '\t')
    {
        text++;
    }
    for (int length = (int)strlen(text); length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'); length--)
    {
        text[length - 1] = '\0';
    }
    return text;
}

// 1/0, on/off, yes/no, true/false; anything else leaves *out alone
static void parse_setting_bool(const char *value, bool *out)
{
    if (_stricmp(value, "1") == 0 || _stricmp(value, "on") == 0 || _stricmp(value, "yes") == 0 || _stricmp(value, "true") == 0)
    {
        *out = true;
    }
    else if (_stricmp(value, "0") == 0 || _stricmp(value, "off") == 0 || _stricmp(value, "no") == 0 || _stricmp(value, "false") == 0)
    {
        *out = false;
    }
}

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

    char *line = text;
    while (*line != '\0')
    {
        char *next = line;
        while (*next != '\0' && *next != '\r' && *next != '\n')
        {
            next++;
        }
        if (*next != '\0')
        {
            *next++ = '\0';
        }

        char *equals = strchr(line, '=');
        if (equals != NULL)
        {
            *equals = '\0';
            const char *key = trim_setting(line);
            const char *value = trim_setting(equals + 1);

            if (_stricmp(key, SETTING_THEME) == 0)
            {
                const int index = Renderer::FindTheme(value);
                if (index >= 0)
                {
                    m_Ui.SetTheme(index);
                }
            }
            else if (_stricmp(key, SETTING_ANIMATIONS) == 0)
            {
                parse_setting_bool(value, &m_Animations);
            }
            else if (_stricmp(key, SETTING_FORCE_LBA48_V3) == 0)
            {
                parse_setting_bool(value, &m_ForceLba48V3);
            }
        }

        line = next;
    }
}

// Best effort and silent (DVD, or D:'s partition changed). Writes every
// setting, so hand edited ones survive a theme change.
void App::SaveSettings()
{
    if (m_ThemeSaveBlocked)
    {
        return;
    }

    // Theme names are plain ASCII
    char text[160];
    const wchar_t *name = m_Ui.GetTheme().name;
    int length = sprintf(text, "%s=", SETTING_THEME);
    for (int i = 0; name[i] != L'\0' && length < 80; i++)
    {
        text[length++] = (char)name[i];
    }
    length += sprintf(text + length, "\r\n%s=%d\r\n%s=%d\r\n", SETTING_ANIMATIONS, m_Animations ? 1 : 0,
                      SETTING_FORCE_LBA48_V3, m_ForceLba48V3 ? 1 : 0);

    HANDLE file = CreateFile(SETTINGS_PATH, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD written = 0;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

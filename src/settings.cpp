#include "settings.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{

const char SETTING_THEME[] = "theme";
const char SETTING_ANIMATIONS[] = "animations";
const char SETTING_FORCE_LBA48_V3[] = "force_lba48_v3";
const char SETTING_RESIZE_KEEPS_DATA[] = "resize_keeps_data";

// Longest file Parse() reads, like the old fixed buffer
const size_t SETTINGS_TEXT_MAX = 511;

bool same_key(const char *a, const char *b)
{
    return _stricmp(a, b) == 0;
}

char *trim_setting(char *text)
{
    while (*text == ' ' || *text == '\t')
    {
        text++;
    }
    for (size_t length = strlen(text); length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'); length--)
    {
        text[length - 1] = '\0';
    }
    return text;
}

// 1/0, on/off, yes/no, true/false; anything else leaves *out alone
void parse_setting_bool(const char *value, bool *out)
{
    if (same_key(value, "1") || same_key(value, "on") || same_key(value, "yes") || same_key(value, "true"))
    {
        *out = true;
    }
    else if (same_key(value, "0") || same_key(value, "off") || same_key(value, "no") || same_key(value, "false"))
    {
        *out = false;
    }
}

// Cuts the line at its end; returns where the next one starts
char *end_line(char *line)
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
    return next;
}

void copy_text(char *out, size_t out_size, const char *text)
{
    size_t length = 0;
    for (; text[length] != '\0' && length < out_size - 1; length++)
    {
        out[length] = text[length];
    }
    out[length] = '\0';
}

// Returns the length, cut to fit out_size
size_t format_text(char *out, size_t out_size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
#ifdef _XBOX
    _vsnprintf(out, out_size, format, args); // No vsnprintf in the XDK runtime
#else
    vsnprintf(out, out_size, format, args);
#endif
    va_end(args);
    out[out_size - 1] = '\0';
    return strlen(out);
}

} // namespace

Settings::Settings() : m_animations(true), m_force_lba48_v3(false), m_resize_keeps_data(false)
{
    m_theme_name[0] = '\0';
}

void Settings::Parse(const char *text)
{
    char copy[SETTINGS_TEXT_MAX + 1];
    copy_text(copy, sizeof(copy), text);

    char *line = copy;
    while (*line != '\0')
    {
        char *next = end_line(line);
        ParseLine(line);
        line = next;
    }
}

void Settings::ParseLine(char *line)
{
    char *equals = strchr(line, '=');
    if (equals == NULL)
    {
        return;
    }
    *equals = '\0';
    ApplySetting(trim_setting(line), trim_setting(equals + 1));
}

void Settings::ApplySetting(const char *key, const char *value)
{
    if (same_key(key, SETTING_THEME))
    {
        copy_text(m_theme_name, THEME_NAME_SIZE, value);
    }
    else if (same_key(key, SETTING_ANIMATIONS))
    {
        parse_setting_bool(value, &m_animations);
    }
    else if (same_key(key, SETTING_FORCE_LBA48_V3))
    {
        parse_setting_bool(value, &m_force_lba48_v3);
    }
    else if (same_key(key, SETTING_RESIZE_KEEPS_DATA))
    {
        parse_setting_bool(value, &m_resize_keeps_data);
    }
}

void Settings::SetThemeName(const wchar_t *name)
{
    size_t length = 0;
    for (; name[length] != L'\0' && length < THEME_NAME_SIZE - 1; length++)
    {
        m_theme_name[length] = (char)name[length];
    }
    m_theme_name[length] = '\0';
}

size_t Settings::Format(char *out, size_t out_size) const
{
    return format_text(out,
                       out_size,
                       "%s=%s\r\n%s=%d\r\n%s=%d\r\n%s=%d\r\n",
                       SETTING_THEME,
                       m_theme_name,
                       SETTING_ANIMATIONS,
                       m_animations ? 1 : 0,
                       SETTING_FORCE_LBA48_V3,
                       m_force_lba48_v3 ? 1 : 0,
                       SETTING_RESIZE_KEEPS_DATA,
                       m_resize_keeps_data ? 1 : 0);
}

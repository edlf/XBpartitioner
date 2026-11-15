#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stddef.h>

// The settings in xbpartitioner.ini, and its text. One key=value per line,
// keys in any case, spaces ignored; missing or unknown values keep the
// default (see the README):
//   theme=Name, animations=0/1, force_lba48_v3=0/1, resize_keeps_data=0/1
class Settings
{
  public:
    Settings();

    // Reads what it knows from the file's text, the rest stays as it is
    void Parse(const char *text);
    // Every setting, so hand edited ones survive a theme change. Returns the
    // length, cut to fit out_size.
    size_t Format(char *out, size_t out_size) const;

    // Empty if the file names none
    const char *ThemeName() const
    {
        return m_theme_name;
    }
    // Theme names are plain ASCII
    void SetThemeName(const wchar_t *name);

    bool Animations() const
    {
        return m_animations;
    }
    bool ForceLba48V3() const
    {
        return m_force_lba48_v3;
    }
    bool ResizeKeepsData() const
    {
        return m_resize_keeps_data;
    }
    void ToggleResizeKeepsData()
    {
        m_resize_keeps_data = !m_resize_keeps_data;
    }

  private:
    void ParseLine(char *line);
    void ApplySetting(const char *key, const char *value);

    static const size_t THEME_NAME_SIZE = 64;
    char m_theme_name[THEME_NAME_SIZE];
    bool m_animations;
    bool m_force_lba48_v3;
    bool m_resize_keeps_data;
};

#endif // SETTINGS_H_

#ifndef THEME_H_
#define THEME_H_

#include "d3dcolor.h"

// Text colors by role, named after the default theme's. Order must match
// Theme::text and g_ColorOpaque in ui.cpp.
enum Color
{
    COLOR_WHITE = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_WHITE_BG,
    COLOR_RED_BG,
    COLOR_GREY,
    COLOR_DARK_YELLOW,
    COLOR_LIGHT_GREY, // Body text on warning screens
    COLOR_WARNING,    // Warning screen title
    COLOR_COUNT
};

struct Theme
{
    LPCWSTR name;
    D3DCOLOR background;        // Behind everything
    D3DCOLOR text[COLOR_COUNT]; // Indexed by Color
    D3DCOLOR text_background;   // Box behind the *_BG text colors
    D3DCOLOR panel;             // Menus, dialogs, the table (the alpha shows what's behind)
    D3DCOLOR panel_border;
    D3DCOLOR panel_line;        // Under titles and the table headers
    D3DCOLOR selection;         // Selection bars, the selected button or key
    D3DCOLOR button;            // Dialog buttons and keypad keys
    D3DCOLOR map_free;          // Disk map: free space,
    D3DCOLOR map_standard;      // standard partitions (1-5),
    D3DCOLOR map_extended;      // extended partitions (6-14)
    D3DCOLOR map_selected;      // and the selected one
};

#endif // THEME_H_

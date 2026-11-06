#include "stdafx.h"
#include "ui.h"

// Fonts, generated from fonts\ by tools\makefonts.bat.
// makefont declares the arrays as "_declspec(selectany) const BYTE", which is
// invalid C++ (error C2496: selectany needs external linkage, and const data
// at file scope has internal linkage). Defining _declspec away while including
// them turns them into plain const arrays, which is all that is needed since
// they are only included here. The *_720 fonts are 1.5x for 720p.
#define _declspec(x)
#include "font_text.h"
#include "font_title.h"
#include "font_table.h"
#include "font_text_720.h"
#include "font_title_720.h"
#include "font_table_720.h"
#undef _declspec

struct ScreenVertex
{
    D3DXVECTOR4 pos; // Pre-transformed screen position
    DWORD color;
};

#define D3DFVF_SCREENVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

// Text colors drawn on a box (Theme::text_background), indexed by the Color enum
static const bool g_ColorOpaque[COLOR_COUNT] = {
    false, // COLOR_WHITE
    false, // COLOR_RED
    false, // COLOR_GREEN
    false, // COLOR_BLUE
    false, // COLOR_YELLOW
    true,  // COLOR_WHITE_BG
    true,  // COLOR_RED_BG
    false, // COLOR_GREY
    false, // COLOR_DARK_YELLOW
    false, // COLOR_LIGHT_GREY
    false, // COLOR_WARNING
};

// The themes, the first one (Outrun) is the default. Only colors: the layout and the
// fonts are the same in all of them. The text roles keep their meaning (red
// for errors, green for success, COLOR_YELLOW for the selected item) even
// when the hue is different.
static const Theme g_Themes[] = {
    {
        L"Outrun",
        D3DCOLOR_XRGB(16, 2, 24), // background
        {
            D3DCOLOR_XRGB(255, 230, 240), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 60, 90), // COLOR_RED
            D3DCOLOR_XRGB(90, 255, 180), // COLOR_GREEN
            D3DCOLOR_XRGB(120, 140, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 200, 60), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 50, 80), // COLOR_RED_BG
            D3DCOLOR_XRGB(170, 110, 160), // COLOR_GREY
            D3DCOLOR_XRGB(200, 130, 40), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(245, 220, 235), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 110, 40), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(60, 15, 60), // text_background
        D3DCOLOR_ARGB(235, 40, 6, 50), // panel
        D3DCOLOR_XRGB(255, 110, 60), // panel_border
        D3DCOLOR_XRGB(150, 50, 110), // panel_line
        D3DCOLOR_XRGB(140, 20, 90), // selection
        D3DCOLOR_XRGB(60, 14, 60), // button
        D3DCOLOR_XRGB(55, 15, 60), // map_free
        D3DCOLOR_XRGB(255, 60, 150), // map_standard
        D3DCOLOR_XRGB(255, 140, 40), // map_extended
        D3DCOLOR_XRGB(255, 240, 120), // map_selected
    },
    {
        L"Synthwave",
        D3DCOLOR_XRGB(12, 4, 28), // background
        {
            D3DCOLOR_XRGB(240, 225, 255), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 70, 110), // COLOR_RED
            D3DCOLOR_XRGB(60, 255, 200), // COLOR_GREEN
            D3DCOLOR_XRGB(120, 150, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(0, 240, 255), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 60, 100), // COLOR_RED_BG
            D3DCOLOR_XRGB(150, 110, 190), // COLOR_GREY
            D3DCOLOR_XRGB(200, 120, 220), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(230, 215, 250), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 120, 60), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(50, 20, 80), // text_background
        D3DCOLOR_ARGB(235, 28, 8, 56), // panel
        D3DCOLOR_XRGB(255, 60, 200), // panel_border
        D3DCOLOR_XRGB(120, 50, 150), // panel_line
        D3DCOLOR_XRGB(90, 20, 120), // selection
        D3DCOLOR_XRGB(45, 15, 75), // button
        D3DCOLOR_XRGB(40, 15, 70), // map_free
        D3DCOLOR_XRGB(0, 200, 255), // map_standard
        D3DCOLOR_XRGB(255, 60, 200), // map_extended
        D3DCOLOR_XRGB(255, 230, 80), // map_selected
    },
    {
        L"Neon grid",
        D3DCOLOR_XRGB(4, 6, 26), // background
        {
            D3DCOLOR_XRGB(220, 240, 255), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 70, 120), // COLOR_RED
            D3DCOLOR_XRGB(70, 255, 170), // COLOR_GREEN
            D3DCOLOR_XRGB(110, 160, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 90, 230), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 60, 110), // COLOR_RED_BG
            D3DCOLOR_XRGB(100, 130, 180), // COLOR_GREY
            D3DCOLOR_XRGB(170, 110, 230), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(210, 225, 245), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 130, 70), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(20, 30, 80), // text_background
        D3DCOLOR_ARGB(235, 10, 14, 48), // panel
        D3DCOLOR_XRGB(0, 230, 255), // panel_border
        D3DCOLOR_XRGB(40, 110, 160), // panel_line
        D3DCOLOR_XRGB(30, 30, 110), // selection
        D3DCOLOR_XRGB(18, 24, 70), // button
        D3DCOLOR_XRGB(18, 24, 70), // map_free
        D3DCOLOR_XRGB(0, 200, 255), // map_standard
        D3DCOLOR_XRGB(190, 80, 255), // map_extended
        D3DCOLOR_XRGB(255, 90, 230), // map_selected
    },
    {
        L"Midnight",
        D3DCOLOR_XRGB(5, 8, 20), // background
        {
            D3DCOLOR_XRGB(225, 232, 255), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 90, 90), // COLOR_RED
            D3DCOLOR_XRGB(90, 230, 140), // COLOR_GREEN
            D3DCOLOR_XRGB(120, 160, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(120, 200, 255), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 60, 60), // COLOR_RED_BG
            D3DCOLOR_XRGB(120, 130, 160), // COLOR_GREY
            D3DCOLOR_XRGB(90, 150, 200), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(210, 215, 235), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 90, 70), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(25, 30, 55), // text_background
        D3DCOLOR_ARGB(240, 12, 18, 40), // panel
        D3DCOLOR_XRGB(110, 150, 255), // panel_border
        D3DCOLOR_XRGB(60, 80, 140), // panel_line
        D3DCOLOR_XRGB(25, 50, 110), // selection
        D3DCOLOR_XRGB(25, 32, 60), // button
        D3DCOLOR_XRGB(25, 32, 60), // map_free
        D3DCOLOR_XRGB(60, 120, 255), // map_standard
        D3DCOLOR_XRGB(140, 90, 255), // map_extended
        D3DCOLOR_XRGB(120, 220, 255), // map_selected
    },
    {
        L"Lime",
        D3DCOLOR_XRGB(0, 8, 0), // background
        {
            D3DCOLOR_XRGB(200, 255, 200), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 80, 60), // COLOR_RED
            D3DCOLOR_XRGB(120, 255, 80), // COLOR_GREEN
            D3DCOLOR_XRGB(80, 200, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(200, 255, 0), // COLOR_YELLOW
            D3DCOLOR_XRGB(230, 255, 230), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 60, 40), // COLOR_RED_BG
            D3DCOLOR_XRGB(90, 150, 90), // COLOR_GREY
            D3DCOLOR_XRGB(130, 180, 0), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(190, 230, 190), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 80, 40), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(10, 40, 10), // text_background
        D3DCOLOR_ARGB(240, 5, 25, 5), // panel
        D3DCOLOR_XRGB(120, 230, 60), // panel_border
        D3DCOLOR_XRGB(50, 120, 40), // panel_line
        D3DCOLOR_XRGB(30, 80, 10), // selection
        D3DCOLOR_XRGB(15, 45, 15), // button
        D3DCOLOR_XRGB(15, 45, 15), // map_free
        D3DCOLOR_XRGB(60, 170, 60), // map_standard
        D3DCOLOR_XRGB(150, 230, 40), // map_extended
        D3DCOLOR_XRGB(255, 255, 120), // map_selected
    },
    {
        L"Crimson",
        D3DCOLOR_XRGB(12, 0, 0), // background
        {
            D3DCOLOR_XRGB(255, 235, 235), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 90, 90), // COLOR_RED
            D3DCOLOR_XRGB(110, 230, 120), // COLOR_GREEN
            D3DCOLOR_XRGB(140, 150, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 200, 80), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 50, 50), // COLOR_RED_BG
            D3DCOLOR_XRGB(160, 110, 110), // COLOR_GREY
            D3DCOLOR_XRGB(190, 130, 40), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(240, 215, 215), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 170, 40), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(50, 15, 15), // text_background
        D3DCOLOR_ARGB(240, 35, 6, 8), // panel
        D3DCOLOR_XRGB(230, 80, 80), // panel_border
        D3DCOLOR_XRGB(130, 50, 50), // panel_line
        D3DCOLOR_XRGB(110, 15, 25), // selection
        D3DCOLOR_XRGB(60, 18, 20), // button
        D3DCOLOR_XRGB(60, 18, 20), // map_free
        D3DCOLOR_XRGB(200, 60, 60), // map_standard
        D3DCOLOR_XRGB(240, 140, 60), // map_extended
        D3DCOLOR_XRGB(255, 230, 100), // map_selected
    },
    {
        L"Amber",
        D3DCOLOR_XRGB(8, 5, 0), // background
        {
            D3DCOLOR_XRGB(255, 176, 0), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 90, 30), // COLOR_RED
            D3DCOLOR_XRGB(200, 230, 80), // COLOR_GREEN
            D3DCOLOR_XRGB(255, 200, 120), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 230, 150), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 200, 80), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 90, 30), // COLOR_RED_BG
            D3DCOLOR_XRGB(150, 100, 20), // COLOR_GREY
            D3DCOLOR_XRGB(180, 120, 0), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(240, 170, 40), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 90, 30), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(40, 25, 0), // text_background
        D3DCOLOR_ARGB(240, 25, 15, 0), // panel
        D3DCOLOR_XRGB(255, 176, 0), // panel_border
        D3DCOLOR_XRGB(130, 85, 0), // panel_line
        D3DCOLOR_XRGB(80, 50, 0), // selection
        D3DCOLOR_XRGB(45, 28, 0), // button
        D3DCOLOR_XRGB(45, 28, 0), // map_free
        D3DCOLOR_XRGB(180, 110, 0), // map_standard
        D3DCOLOR_XRGB(255, 176, 0), // map_extended
        D3DCOLOR_XRGB(255, 240, 180), // map_selected
    },
    {
        L"Ocean",
        D3DCOLOR_XRGB(0, 10, 14), // background
        {
            D3DCOLOR_XRGB(220, 250, 250), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 100, 90), // COLOR_RED
            D3DCOLOR_XRGB(80, 240, 170), // COLOR_GREEN
            D3DCOLOR_XRGB(90, 180, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(100, 255, 230), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 70, 60), // COLOR_RED_BG
            D3DCOLOR_XRGB(100, 150, 160), // COLOR_GREY
            D3DCOLOR_XRGB(60, 180, 170), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(200, 235, 235), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 110, 80), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(10, 40, 50), // text_background
        D3DCOLOR_ARGB(240, 4, 30, 40), // panel
        D3DCOLOR_XRGB(80, 210, 220), // panel_border
        D3DCOLOR_XRGB(40, 110, 120), // panel_line
        D3DCOLOR_XRGB(0, 80, 95), // selection
        D3DCOLOR_XRGB(10, 50, 60), // button
        D3DCOLOR_XRGB(10, 50, 60), // map_free
        D3DCOLOR_XRGB(0, 150, 170), // map_standard
        D3DCOLOR_XRGB(40, 200, 140), // map_extended
        D3DCOLOR_XRGB(255, 230, 120), // map_selected
    },
    {
        L"Grape",
        D3DCOLOR_XRGB(10, 0, 14), // background
        {
            D3DCOLOR_XRGB(245, 230, 255), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 90, 120), // COLOR_RED
            D3DCOLOR_XRGB(120, 240, 160), // COLOR_GREEN
            D3DCOLOR_XRGB(150, 140, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 150, 255), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 60, 90), // COLOR_RED_BG
            D3DCOLOR_XRGB(150, 120, 170), // COLOR_GREY
            D3DCOLOR_XRGB(200, 120, 200), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(230, 215, 240), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 90, 110), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(45, 20, 55), // text_background
        D3DCOLOR_ARGB(240, 30, 10, 40), // panel
        D3DCOLOR_XRGB(200, 130, 255), // panel_border
        D3DCOLOR_XRGB(110, 70, 140), // panel_line
        D3DCOLOR_XRGB(90, 25, 120), // selection
        D3DCOLOR_XRGB(50, 22, 65), // button
        D3DCOLOR_XRGB(50, 22, 65), // map_free
        D3DCOLOR_XRGB(130, 80, 220), // map_standard
        D3DCOLOR_XRGB(220, 90, 200), // map_extended
        D3DCOLOR_XRGB(255, 240, 120), // map_selected
    },
    {
        L"Slate",
        D3DCOLOR_XRGB(18, 20, 22), // background
        {
            D3DCOLOR_XRGB(235, 238, 240), // COLOR_WHITE
            D3DCOLOR_XRGB(240, 90, 80), // COLOR_RED
            D3DCOLOR_XRGB(110, 210, 120), // COLOR_GREEN
            D3DCOLOR_XRGB(120, 160, 230), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 210, 90), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(240, 70, 60), // COLOR_RED_BG
            D3DCOLOR_XRGB(130, 136, 142), // COLOR_GREY
            D3DCOLOR_XRGB(180, 150, 70), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(215, 220, 225), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(240, 100, 70), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(50, 54, 58), // text_background
        D3DCOLOR_ARGB(245, 36, 40, 44), // panel
        D3DCOLOR_XRGB(170, 176, 182), // panel_border
        D3DCOLOR_XRGB(90, 96, 102), // panel_line
        D3DCOLOR_XRGB(70, 78, 88), // selection
        D3DCOLOR_XRGB(52, 57, 62), // button
        D3DCOLOR_XRGB(52, 57, 62), // map_free
        D3DCOLOR_XRGB(110, 140, 170), // map_standard
        D3DCOLOR_XRGB(150, 160, 175), // map_extended
        D3DCOLOR_XRGB(255, 210, 90), // map_selected
    },
    {
        L"Sunset",
        D3DCOLOR_XRGB(14, 4, 10), // background
        {
            D3DCOLOR_XRGB(255, 235, 220), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 80, 80), // COLOR_RED
            D3DCOLOR_XRGB(140, 230, 120), // COLOR_GREEN
            D3DCOLOR_XRGB(150, 150, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 200, 90), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 60, 60), // COLOR_RED_BG
            D3DCOLOR_XRGB(170, 120, 120), // COLOR_GREY
            D3DCOLOR_XRGB(210, 140, 60), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(245, 220, 210), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 120, 60), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(55, 20, 30), // text_background
        D3DCOLOR_ARGB(240, 40, 12, 30), // panel
        D3DCOLOR_XRGB(255, 140, 90), // panel_border
        D3DCOLOR_XRGB(140, 70, 70), // panel_line
        D3DCOLOR_XRGB(120, 30, 60), // selection
        D3DCOLOR_XRGB(65, 22, 40), // button
        D3DCOLOR_XRGB(65, 22, 40), // map_free
        D3DCOLOR_XRGB(240, 110, 70), // map_standard
        D3DCOLOR_XRGB(200, 60, 130), // map_extended
        D3DCOLOR_XRGB(255, 230, 120), // map_selected
    },
    {
        L"High contrast",
        D3DCOLOR_XRGB(0, 0, 0), // background
        {
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 60, 60), // COLOR_RED
            D3DCOLOR_XRGB(0, 255, 0), // COLOR_GREEN
            D3DCOLOR_XRGB(80, 160, 255), // COLOR_BLUE
            D3DCOLOR_XRGB(255, 255, 0), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 0, 0), // COLOR_RED_BG
            D3DCOLOR_XRGB(200, 200, 200), // COLOR_GREY
            D3DCOLOR_XRGB(255, 200, 0), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 60, 60), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(60, 60, 60), // text_background
        D3DCOLOR_ARGB(255, 0, 0, 0), // panel
        D3DCOLOR_XRGB(255, 255, 255), // panel_border
        D3DCOLOR_XRGB(255, 255, 255), // panel_line
        D3DCOLOR_XRGB(0, 0, 160), // selection
        D3DCOLOR_XRGB(40, 40, 40), // button
        D3DCOLOR_XRGB(40, 40, 40), // map_free
        D3DCOLOR_XRGB(0, 200, 255), // map_standard
        D3DCOLOR_XRGB(255, 0, 255), // map_extended
        D3DCOLOR_XRGB(255, 255, 0), // map_selected
    },
    {
        L"Paper",
        D3DCOLOR_XRGB(225, 222, 212), // background
        {
            D3DCOLOR_XRGB(25, 25, 30), // COLOR_WHITE
            D3DCOLOR_XRGB(190, 20, 20), // COLOR_RED
            D3DCOLOR_XRGB(20, 130, 40), // COLOR_GREEN
            D3DCOLOR_XRGB(30, 60, 190), // COLOR_BLUE
            D3DCOLOR_XRGB(130, 40, 0), // COLOR_YELLOW
            D3DCOLOR_XRGB(0, 0, 0), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(200, 0, 0), // COLOR_RED_BG
            D3DCOLOR_XRGB(110, 105, 95), // COLOR_GREY
            D3DCOLOR_XRGB(140, 110, 0), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(45, 45, 50), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(190, 30, 10), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(200, 196, 185), // text_background
        D3DCOLOR_ARGB(245, 245, 242, 232), // panel
        D3DCOLOR_XRGB(60, 60, 60), // panel_border
        D3DCOLOR_XRGB(150, 145, 135), // panel_line
        D3DCOLOR_XRGB(205, 195, 165), // selection
        D3DCOLOR_XRGB(220, 214, 200), // button
        D3DCOLOR_XRGB(215, 208, 190), // map_free
        D3DCOLOR_XRGB(40, 110, 160), // map_standard
        D3DCOLOR_XRGB(140, 60, 150), // map_extended
        D3DCOLOR_XRGB(220, 120, 0), // map_selected
    },
};
static const int THEME_COUNT = sizeof(g_Themes) / sizeof(g_Themes[0]);

int Renderer::GetThemeCount()
{
    return THEME_COUNT;
}

const Theme &Renderer::GetThemeAt(int index)
{
    return g_Themes[(index >= 0 && index < THEME_COUNT) ? index : 0];
}

// Theme names are ASCII
static wchar_t ascii_lower(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
}

int Renderer::FindTheme(const char *name)
{
    for (int i = 0; i < THEME_COUNT; i++)
    {
        const wchar_t *theme_name = g_Themes[i].name;
        int c = 0;
        while (theme_name[c] != L'\0' && name[c] != '\0' &&
               ascii_lower(theme_name[c]) == ascii_lower((wchar_t)(unsigned char)name[c]))
        {
            c++;
        }
        if (theme_name[c] == L'\0' && name[c] == '\0')
        {
            return i;
        }
    }
    return -1;
}

void Renderer::SetTheme(int index)
{
    if (index >= 0 && index < THEME_COUNT)
    {
        m_ThemeIndex = index;
    }
}

struct FontData
{
    const BYTE *data;
    unsigned int size;
    int height; // Pixel height the font was generated at (makefont -h, tools\makefonts.bat)
};

// Indexed by the Font enum
static const FontData g_Fonts480[FONT_COUNT] = {
    {font_text, sizeof(font_text), 18},
    {font_table, sizeof(font_table), 18},
    {font_title, sizeof(font_title), 32},
};

static const FontData g_Fonts720[FONT_COUNT] = {
    {font_text_720, sizeof(font_text_720), 27},
    {font_table_720, sizeof(font_table_720), 27},
    {font_title_720, sizeof(font_title_720), 48},
};

Renderer::Renderer()
    : m_pD3D(NULL), m_pDevice(NULL), m_pFrameBackBuffer(NULL), m_pSavedFrame(NULL),
      m_SavedFrameValid(false), m_SavedFrameChecked(false), m_SavedFrameBroken(false), m_ThemeIndex(0), m_VideoMode(VIDEO_MODE_480I),
      m_Width(640), m_Height(480), m_OffsetX(0.0f), m_OffsetY(0.0f)
{
    for (int i = 0; i < FONT_COUNT; i++)
    {
        m_pFonts[i] = NULL;
        m_FontHeights[i] = 0;
        m_FontDescents[i] = 0;
        m_FontCapHeights[i] = 0;
    }
}

Renderer::~Renderer()
{
    Shutdown();
}

// Picks the video mode from the AV pack and the dashboard video settings:
//   720p - component (HDTV) pack with 720p enabled
//   480p - component or VGA pack with 480p enabled
//   480i - everything else (composite, s-video, scart, rf)
static VideoMode SelectVideoMode()
{
    const DWORD video_flags = XGetVideoFlags();
    const DWORD av_pack = XGetAVPack();

    if (av_pack == XC_AV_PACK_HDTV && (video_flags & XC_VIDEO_FLAGS_HDTV_720p))
    {
        return VIDEO_MODE_720P;
    }

    if ((av_pack == XC_AV_PACK_HDTV || av_pack == XC_AV_PACK_VGA) && (video_flags & XC_VIDEO_FLAGS_HDTV_480p))
    {
        return VIDEO_MODE_480P;
    }

    return VIDEO_MODE_480I;
}

// Creates the device for one video mode and sets up the layout mapping for it
bool Renderer::CreateDevice(VideoMode mode)
{
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));

    switch (mode)
    {
    case VIDEO_MODE_720P:
        m_Width = 1280;
        m_Height = 720;
        d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
        break;
    case VIDEO_MODE_480P:
        m_Width = 640;
        m_Height = 480;
        d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE;
        break;
    default:
        m_Width = 640;
        m_Height = 480;
        break;
    }

    d3dpp.BackBufferWidth = m_Width;
    d3dpp.BackBufferHeight = m_Height;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    m_pDevice = NULL;
    if (FAILED(m_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &m_pDevice)) || m_pDevice == NULL)
    {
        m_pDevice = NULL;
        return false;
    }

    m_VideoMode = mode;

    // The layout is a 4:3 area the height of the screen, centered horizontally
    // (720p: 960 wide, 160 pixel bars). Whole pixels, so text lands on pixels.
    const unsigned int layout_width = m_Height * 4 / 3;
    m_OffsetX = (FLOAT)((m_Width - layout_width) / 2);
    m_OffsetY = 0.0f;

    return true;
}

void Renderer::Init()
{
    m_pD3D = Direct3DCreate8(D3D_SDK_VERSION);

    // If the selected mode is refused, fall back to plain 480i which every setup supports
    const VideoMode mode = SelectVideoMode();
    if (!CreateDevice(mode) && mode != VIDEO_MODE_480I)
    {
        CreateDevice(VIDEO_MODE_480I);
    }

    // Nothing can be shown without a device, go back to the dashboard
    if (m_pDevice == NULL)
    {
        Shutdown();
        LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};
        XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
        return;
    }

    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);

    // Fonts are pre-rendered for each mode, so text stays sharp at 720p
    const FontData *fonts = (m_VideoMode == VIDEO_MODE_720P) ? g_Fonts720 : g_Fonts480;
    for (int i = 0; i < FONT_COUNT; i++)
    {
        XFONT_OpenBitmapFontFromMemory(fonts[i].data, fonts[i].size, &m_pFonts[i]);

        // Cell height and descent of the font as drawn in this mode, so boxes
        // can be sized around the text and text centered in them. Falls back to
        // the height the font was generated at, and DejaVu's descent (about
        // 0.24 of the height), if XFONT reports nothing (or something
        // implausible) for it.
        m_FontHeights[i] = fonts[i].height;
        m_FontDescents[i] = (fonts[i].height * 24 + 50) / 100;
        // XFONT has no cap height; DejaVu's capitals are 0.73 of the font size
        // (13px at 18, 20px at 27, 23px at 32). Used to center text on its capitals.
        m_FontCapHeights[i] = (fonts[i].height * 73 + 50) / 100;
        if (m_pFonts[i] != NULL)
        {
            unsigned int cell_height = 0;
            unsigned int descent = 0;
            m_pFonts[i]->GetFontMetrics(&cell_height, &descent);
            if (cell_height >= (unsigned int)fonts[i].height && cell_height <= (unsigned int)fonts[i].height * 2)
            {
                m_FontHeights[i] = (int)cell_height;
                if (descent > 0 && descent < cell_height / 2)
                {
                    m_FontDescents[i] = (int)descent;
                }
            }
        }
    }
}

void Renderer::Shutdown()
{
    if (m_pFrameBackBuffer)
    {
        m_pFrameBackBuffer->Release();
        m_pFrameBackBuffer = NULL;
    }

    if (m_pSavedFrame)
    {
        m_pSavedFrame->Release();
        m_pSavedFrame = NULL;
    }
    m_SavedFrameValid = false;

    for (int i = 0; i < FONT_COUNT; i++)
    {
        if (m_pFonts[i])
        {
            m_pFonts[i]->Release();
            m_pFonts[i] = NULL;
        }
    }

    if (m_pDevice)
    {
        m_pDevice->Release();
        m_pDevice = NULL;
    }

    if (m_pD3D)
    {
        m_pD3D->Release();
        m_pD3D = NULL;
    }
}

LPCWSTR Renderer::GetVideoModeName() const
{
    switch (m_VideoMode)
    {
    case VIDEO_MODE_720P:
        return L"720p";
    case VIDEO_MODE_480P:
        return L"480p";
    default:
        return L"480i";
    }
}

void Renderer::BeginFrame()
{
    if (m_pFrameBackBuffer == NULL)
    {
        m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &m_pFrameBackBuffer);
    }

    m_pDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, GetTheme().background, 1.0f, 0);

    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_pDevice->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 255, 255, 255));
}

void Renderer::EndFrame()
{
    // Present swaps the buffers, the next frame looks up its own
    if (m_pFrameBackBuffer)
    {
        m_pFrameBackBuffer->Release();
        m_pFrameBackBuffer = NULL;
    }

    m_pDevice->Present(NULL, NULL, NULL, NULL);
}

void Renderer::Text(Font font, LPCWSTR text, int x, int y, Color color, int align)
{
    XFONT *pFont = m_pFonts[(font >= 0 && font < FONT_COUNT) ? font : FONT_TEXT];
    if (pFont == NULL)
    {
        return;
    }

    const Theme &theme = GetTheme();
    if (color < 0 || color >= COLOR_COUNT)
    {
        color = COLOR_LIGHT_GREY;
    }

    if (g_ColorOpaque[color])
    {
        pFont->SetBkMode(XFONT_OPAQUE);
        pFont->SetBkColor(theme.text_background);
    }
    else
    {
        pFont->SetBkMode(XFONT_TRANSPARENT);
    }
    pFont->SetTextColor(theme.text[color]);
    pFont->SetTextAlignment(XFONT_TOP | align);

    // The fonts are already sized for the video mode, only the offset is added
    const long screen_x = (long)(ToScreenX((FLOAT)x) + 0.5f);
    const long screen_y = (long)(ToScreenY((FLOAT)y) + 0.5f);

    if (m_pFrameBackBuffer)
    {
        pFont->TextOut(m_pFrameBackBuffer, text, -1, screen_x, screen_y);
        return;
    }

    // Outside BeginFrame()/EndFrame()
    LPDIRECT3DSURFACE8 pBackBuffer;
    m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    pFont->TextOut(pBackBuffer, text, -1, screen_x, screen_y);
    pBackBuffer->Release();
}

// Compares a few pixels of two 32 bit surfaces of the same size, reading them
// back (which waits for the GPU). false if they differ or can't be read.
bool Renderer::SameSamplePixels(LPDIRECT3DSURFACE8 a, LPDIRECT3DSURFACE8 b)
{
    D3DSURFACE_DESC desc;
    a->GetDesc(&desc);
    if (desc.Width < 4 || desc.Height < 4)
    {
        return false;
    }

    D3DLOCKED_RECT locked_a, locked_b;
    if (FAILED(a->LockRect(&locked_a, NULL, D3DLOCK_READONLY)))
    {
        return false;
    }
    if (FAILED(b->LockRect(&locked_b, NULL, D3DLOCK_READONLY)))
    {
        a->UnlockRect();
        return false;
    }

    // Corners and the middle: text, panels and the background all differ
    const UINT xs[] = {1, desc.Width / 2, desc.Width - 2, desc.Width / 3};
    const UINT ys[] = {1, desc.Height / 2, desc.Height - 2, desc.Height / 4};
    bool same = true;
    for (int i = 0; i < 4 && same; i++)
    {
        const DWORD pa = *(const DWORD *)((const BYTE *)locked_a.pBits + ys[i] * locked_a.Pitch + xs[i] * 4);
        const DWORD pb = *(const DWORD *)((const BYTE *)locked_b.pBits + ys[i] * locked_b.Pitch + xs[i] * 4);
        same = ((pa & 0x00FFFFFF) == (pb & 0x00FFFFFF));
    }

    b->UnlockRect();
    a->UnlockRect();
    return same;
}

bool Renderer::SaveBuffer(INT which)
{
    m_SavedFrameValid = false;
    if (m_pDevice == NULL || m_SavedFrameBroken)
    {
        return false;
    }

    LPDIRECT3DSURFACE8 pSource = NULL;
    m_pDevice->GetBackBuffer(which, D3DBACKBUFFER_TYPE_MONO, &pSource);
    if (pSource == NULL)
    {
        return false;
    }

    if (m_pSavedFrame == NULL)
    {
        // Same size and format as the buffers, about 1.2 MB (480) or 2.7 MB (720)
        D3DSURFACE_DESC desc;
        pSource->GetDesc(&desc);
        if (FAILED(m_pDevice->CreateImageSurface(desc.Width, desc.Height, desc.Format, &m_pSavedFrame)))
        {
            m_pSavedFrame = NULL;
            pSource->Release();
            return false;
        }
    }

    m_pDevice->CopyRects(pSource, NULL, 0, m_pSavedFrame, NULL);

    // Once: check the copy really happened (surface formats and copies differ
    // between XDK versions). If not, never try again.
    if (!m_SavedFrameChecked)
    {
        m_SavedFrameChecked = true;
        if (!SameSamplePixels(pSource, m_pSavedFrame))
        {
            m_SavedFrameBroken = true;
            pSource->Release();
            return false;
        }
    }

    pSource->Release();
    m_SavedFrameValid = true;
    return true;
}

bool Renderer::SaveBackBuffer()
{
    return SaveBuffer(0);
}

bool Renderer::SaveFrontBuffer()
{
    return SaveBuffer(-1);
}

bool Renderer::RestoreSavedFrame()
{
    if (!m_SavedFrameValid || m_pSavedFrame == NULL)
    {
        return false;
    }

    LPDIRECT3DSURFACE8 pBackBuffer = m_pFrameBackBuffer;
    if (pBackBuffer == NULL)
    {
        m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    }
    else
    {
        pBackBuffer->AddRef();
    }

    if (pBackBuffer == NULL)
    {
        return false;
    }

    m_pDevice->CopyRects(m_pSavedFrame, NULL, 0, pBackBuffer, NULL);
    pBackBuffer->Release();
    return true;
}

int Renderer::FontHeight(Font font) const
{
    return (font >= 0 && font < FONT_COUNT) ? m_FontHeights[font] : 0;
}

int Renderer::FontDescent(Font font) const
{
    return (font >= 0 && font < FONT_COUNT) ? m_FontDescents[font] : 0;
}

int Renderer::FontCapHeight(Font font) const
{
    return (font >= 0 && font < FONT_COUNT) ? m_FontCapHeights[font] : 0;
}

void Renderer::Rect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color)
{
    ScreenRect(ToScreenX(x1), ToScreenY(y1), ToScreenX(x2), ToScreenY(y2), top_color, bottom_color);
}

void Renderer::Outline(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR color)
{
    Rect(x1, y1, x2, y1 + 1.0f, color, color);         // Top
    Rect(x1, y2 - 1.0f, x2, y2, color, color);         // Bottom
    Rect(x1, y1 + 1.0f, x1 + 1.0f, y2 - 1.0f, color, color); // Left
    Rect(x2 - 1.0f, y1 + 1.0f, x2, y2 - 1.0f, color, color); // Right
}

void Renderer::FillScreen(D3DCOLOR color)
{
    ScreenRect(0.0f, 0.0f, (FLOAT)m_Width, (FLOAT)m_Height, color, color);
}

void Renderer::FadeOut()
{
    LPDIRECT3DSURFACE8 pFrontBuffer, pBackBuffer;
    m_pDevice->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);
    m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

    // Redraw the last frame with an increasingly opaque black overlay
    for (DWORD alpha = 0; alpha < 0xfe; alpha += 2)
    {
        m_pDevice->CopyRects(pFrontBuffer, 0, 0, pBackBuffer, 0);
        FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        EndFrame();
    }

    pFrontBuffer->Release();
    pBackBuffer->Release();
}

void Renderer::ScreenRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color)
{
    ScreenVertex v[4];
    v[0].pos = D3DXVECTOR4(x1 - 0.5f, y1 - 0.5f, 1.0f, 1.0f);
    v[0].color = top_color;
    v[1].pos = D3DXVECTOR4(x2 - 0.5f, y1 - 0.5f, 1.0f, 1.0f);
    v[1].color = top_color;
    v[2].pos = D3DXVECTOR4(x1 - 0.5f, y2 - 0.5f, 1.0f, 1.0f);
    v[2].color = bottom_color;
    v[3].pos = D3DXVECTOR4(x2 - 0.5f, y2 - 0.5f, 1.0f, 1.0f);
    v[3].color = bottom_color;

    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    m_pDevice->SetVertexShader(D3DFVF_SCREENVERTEX);
    m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(ScreenVertex));
}

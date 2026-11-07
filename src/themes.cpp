#include "stdafx.h"
#include "themes.h"

// The first one (Outrun) is the default. The text roles keep their meaning
// (red errors, green success, COLOR_YELLOW selected) in every theme.
const Theme g_Themes[] = {
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
        D3DCOLOR_XRGB(255, 230, 80), // map_selected
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
    // Inspired by common terminal and editor color schemes
    {
        L"Monokai",
        D3DCOLOR_XRGB(39, 40, 34), // background
        {
            D3DCOLOR_XRGB(248, 248, 242), // COLOR_WHITE
            D3DCOLOR_XRGB(249, 38, 114), // COLOR_RED
            D3DCOLOR_XRGB(166, 226, 46), // COLOR_GREEN
            D3DCOLOR_XRGB(102, 217, 239), // COLOR_BLUE
            D3DCOLOR_XRGB(230, 219, 116), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(249, 38, 114), // COLOR_RED_BG
            D3DCOLOR_XRGB(154, 150, 126), // COLOR_GREY
            D3DCOLOR_XRGB(253, 151, 31), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(230, 230, 220), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(253, 151, 31), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(62, 61, 50), // text_background
        D3DCOLOR_ARGB(240, 49, 50, 43), // panel
        D3DCOLOR_XRGB(102, 217, 239), // panel_border
        D3DCOLOR_XRGB(117, 113, 94), // panel_line
        D3DCOLOR_XRGB(73, 72, 62), // selection
        D3DCOLOR_XRGB(62, 61, 50), // button
        D3DCOLOR_XRGB(62, 61, 50), // map_free
        D3DCOLOR_XRGB(249, 38, 114), // map_standard
        D3DCOLOR_XRGB(174, 129, 255), // map_extended
        D3DCOLOR_XRGB(230, 219, 116), // map_selected
    },
    {
        L"Solarized dark",
        D3DCOLOR_XRGB(0, 43, 54), // background
        {
            D3DCOLOR_XRGB(147, 161, 161), // COLOR_WHITE
            D3DCOLOR_XRGB(220, 50, 47), // COLOR_RED
            D3DCOLOR_XRGB(133, 153, 0), // COLOR_GREEN
            D3DCOLOR_XRGB(38, 139, 210), // COLOR_BLUE
            D3DCOLOR_XRGB(181, 137, 0), // COLOR_YELLOW
            D3DCOLOR_XRGB(253, 246, 227), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(220, 50, 47), // COLOR_RED_BG
            D3DCOLOR_XRGB(101, 123, 131), // COLOR_GREY
            D3DCOLOR_XRGB(203, 75, 22), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(238, 232, 213), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(203, 75, 22), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(7, 54, 66), // text_background
        D3DCOLOR_ARGB(240, 7, 54, 66), // panel
        D3DCOLOR_XRGB(38, 139, 210), // panel_border
        D3DCOLOR_XRGB(88, 110, 117), // panel_line
        D3DCOLOR_XRGB(14, 74, 90), // selection
        D3DCOLOR_XRGB(7, 54, 66), // button
        D3DCOLOR_XRGB(7, 54, 66), // map_free
        D3DCOLOR_XRGB(38, 139, 210), // map_standard
        D3DCOLOR_XRGB(42, 161, 152), // map_extended
        D3DCOLOR_XRGB(181, 137, 0), // map_selected
    },
    {
        L"Solarized light",
        D3DCOLOR_XRGB(253, 246, 227), // background
        {
            D3DCOLOR_XRGB(7, 54, 66), // COLOR_WHITE
            D3DCOLOR_XRGB(220, 50, 47), // COLOR_RED
            D3DCOLOR_XRGB(133, 153, 0), // COLOR_GREEN
            D3DCOLOR_XRGB(38, 139, 210), // COLOR_BLUE
            D3DCOLOR_XRGB(203, 75, 22), // COLOR_YELLOW
            D3DCOLOR_XRGB(0, 43, 54), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(220, 50, 47), // COLOR_RED_BG
            D3DCOLOR_XRGB(147, 161, 161), // COLOR_GREY
            D3DCOLOR_XRGB(181, 137, 0), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(88, 110, 117), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(220, 50, 47), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(238, 232, 213), // text_background
        D3DCOLOR_ARGB(245, 238, 232, 213), // panel
        D3DCOLOR_XRGB(88, 110, 117), // panel_border
        D3DCOLOR_XRGB(147, 161, 161), // panel_line
        D3DCOLOR_XRGB(217, 210, 188), // selection
        D3DCOLOR_XRGB(228, 221, 200), // button
        D3DCOLOR_XRGB(228, 221, 200), // map_free
        D3DCOLOR_XRGB(38, 139, 210), // map_standard
        D3DCOLOR_XRGB(108, 113, 196), // map_extended
        D3DCOLOR_XRGB(203, 75, 22), // map_selected
    },
    {
        L"Dracula",
        D3DCOLOR_XRGB(40, 42, 54), // background
        {
            D3DCOLOR_XRGB(248, 248, 242), // COLOR_WHITE
            D3DCOLOR_XRGB(255, 85, 85), // COLOR_RED
            D3DCOLOR_XRGB(80, 250, 123), // COLOR_GREEN
            D3DCOLOR_XRGB(139, 233, 253), // COLOR_BLUE
            D3DCOLOR_XRGB(241, 250, 140), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(255, 85, 85), // COLOR_RED_BG
            D3DCOLOR_XRGB(120, 135, 190), // COLOR_GREY
            D3DCOLOR_XRGB(255, 184, 108), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(230, 230, 235), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 184, 108), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(68, 71, 90), // text_background
        D3DCOLOR_ARGB(240, 33, 34, 44), // panel
        D3DCOLOR_XRGB(189, 147, 249), // panel_border
        D3DCOLOR_XRGB(98, 114, 164), // panel_line
        D3DCOLOR_XRGB(68, 71, 90), // selection
        D3DCOLOR_XRGB(52, 55, 70), // button
        D3DCOLOR_XRGB(52, 55, 70), // map_free
        D3DCOLOR_XRGB(189, 147, 249), // map_standard
        D3DCOLOR_XRGB(255, 121, 198), // map_extended
        D3DCOLOR_XRGB(241, 250, 140), // map_selected
    },
    {
        L"Nord",
        D3DCOLOR_XRGB(46, 52, 64), // background
        {
            D3DCOLOR_XRGB(236, 239, 244), // COLOR_WHITE
            D3DCOLOR_XRGB(191, 97, 106), // COLOR_RED
            D3DCOLOR_XRGB(163, 190, 140), // COLOR_GREEN
            D3DCOLOR_XRGB(129, 161, 193), // COLOR_BLUE
            D3DCOLOR_XRGB(136, 192, 208), // COLOR_YELLOW
            D3DCOLOR_XRGB(236, 239, 244), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(191, 97, 106), // COLOR_RED_BG
            D3DCOLOR_XRGB(124, 133, 150), // COLOR_GREY
            D3DCOLOR_XRGB(208, 135, 112), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(216, 222, 233), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(208, 135, 112), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(59, 66, 82), // text_background
        D3DCOLOR_ARGB(240, 59, 66, 82), // panel
        D3DCOLOR_XRGB(136, 192, 208), // panel_border
        D3DCOLOR_XRGB(76, 86, 106), // panel_line
        D3DCOLOR_XRGB(67, 76, 94), // selection
        D3DCOLOR_XRGB(59, 66, 82), // button
        D3DCOLOR_XRGB(59, 66, 82), // map_free
        D3DCOLOR_XRGB(94, 129, 172), // map_standard
        D3DCOLOR_XRGB(143, 188, 187), // map_extended
        D3DCOLOR_XRGB(235, 203, 139), // map_selected
    },
    {
        L"Gruvbox",
        D3DCOLOR_XRGB(40, 40, 40), // background
        {
            D3DCOLOR_XRGB(235, 219, 178), // COLOR_WHITE
            D3DCOLOR_XRGB(251, 73, 52), // COLOR_RED
            D3DCOLOR_XRGB(184, 187, 38), // COLOR_GREEN
            D3DCOLOR_XRGB(131, 165, 152), // COLOR_BLUE
            D3DCOLOR_XRGB(250, 189, 47), // COLOR_YELLOW
            D3DCOLOR_XRGB(251, 241, 199), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(251, 73, 52), // COLOR_RED_BG
            D3DCOLOR_XRGB(146, 131, 116), // COLOR_GREY
            D3DCOLOR_XRGB(215, 153, 33), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(213, 196, 161), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(254, 128, 25), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(60, 56, 54), // text_background
        D3DCOLOR_ARGB(240, 50, 48, 47), // panel
        D3DCOLOR_XRGB(215, 153, 33), // panel_border
        D3DCOLOR_XRGB(102, 92, 84), // panel_line
        D3DCOLOR_XRGB(80, 73, 69), // selection
        D3DCOLOR_XRGB(60, 56, 54), // button
        D3DCOLOR_XRGB(60, 56, 54), // map_free
        D3DCOLOR_XRGB(254, 128, 25), // map_standard
        D3DCOLOR_XRGB(142, 192, 124), // map_extended
        D3DCOLOR_XRGB(250, 189, 47), // map_selected
    },
    {
        L"One dark",
        D3DCOLOR_XRGB(40, 44, 52), // background
        {
            D3DCOLOR_XRGB(215, 218, 224), // COLOR_WHITE
            D3DCOLOR_XRGB(224, 108, 117), // COLOR_RED
            D3DCOLOR_XRGB(152, 195, 121), // COLOR_GREEN
            D3DCOLOR_XRGB(97, 175, 239), // COLOR_BLUE
            D3DCOLOR_XRGB(229, 192, 123), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(224, 108, 117), // COLOR_RED_BG
            D3DCOLOR_XRGB(127, 132, 142), // COLOR_GREY
            D3DCOLOR_XRGB(209, 154, 102), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(171, 178, 191), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(209, 154, 102), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(62, 68, 81), // text_background
        D3DCOLOR_ARGB(240, 33, 37, 43), // panel
        D3DCOLOR_XRGB(97, 175, 239), // panel_border
        D3DCOLOR_XRGB(92, 99, 112), // panel_line
        D3DCOLOR_XRGB(62, 68, 81), // selection
        D3DCOLOR_XRGB(44, 49, 58), // button
        D3DCOLOR_XRGB(44, 49, 58), // map_free
        D3DCOLOR_XRGB(97, 175, 239), // map_standard
        D3DCOLOR_XRGB(198, 120, 221), // map_extended
        D3DCOLOR_XRGB(229, 192, 123), // map_selected
    },
    {
        L"Tokyo night",
        D3DCOLOR_XRGB(26, 27, 38), // background
        {
            D3DCOLOR_XRGB(192, 202, 245), // COLOR_WHITE
            D3DCOLOR_XRGB(247, 118, 142), // COLOR_RED
            D3DCOLOR_XRGB(158, 206, 106), // COLOR_GREEN
            D3DCOLOR_XRGB(122, 162, 247), // COLOR_BLUE
            D3DCOLOR_XRGB(224, 175, 104), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(247, 118, 142), // COLOR_RED_BG
            D3DCOLOR_XRGB(115, 122, 162), // COLOR_GREY
            D3DCOLOR_XRGB(255, 158, 100), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(169, 177, 214), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(255, 158, 100), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(41, 46, 66), // text_background
        D3DCOLOR_ARGB(240, 22, 22, 30), // panel
        D3DCOLOR_XRGB(122, 162, 247), // panel_border
        D3DCOLOR_XRGB(65, 72, 104), // panel_line
        D3DCOLOR_XRGB(51, 70, 124), // selection
        D3DCOLOR_XRGB(36, 40, 59), // button
        D3DCOLOR_XRGB(36, 40, 59), // map_free
        D3DCOLOR_XRGB(122, 162, 247), // map_standard
        D3DCOLOR_XRGB(187, 154, 247), // map_extended
        D3DCOLOR_XRGB(224, 175, 104), // map_selected
    },
    {
        L"Catppuccin",
        D3DCOLOR_XRGB(30, 30, 46), // background
        {
            D3DCOLOR_XRGB(205, 214, 244), // COLOR_WHITE
            D3DCOLOR_XRGB(243, 139, 168), // COLOR_RED
            D3DCOLOR_XRGB(166, 227, 161), // COLOR_GREEN
            D3DCOLOR_XRGB(137, 180, 250), // COLOR_BLUE
            D3DCOLOR_XRGB(249, 226, 175), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(243, 139, 168), // COLOR_RED_BG
            D3DCOLOR_XRGB(127, 132, 156), // COLOR_GREY
            D3DCOLOR_XRGB(250, 179, 135), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(186, 194, 222), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(250, 179, 135), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(49, 50, 68), // text_background
        D3DCOLOR_ARGB(240, 24, 24, 37), // panel
        D3DCOLOR_XRGB(203, 166, 247), // panel_border
        D3DCOLOR_XRGB(69, 71, 90), // panel_line
        D3DCOLOR_XRGB(69, 71, 90), // selection
        D3DCOLOR_XRGB(49, 50, 68), // button
        D3DCOLOR_XRGB(49, 50, 68), // map_free
        D3DCOLOR_XRGB(203, 166, 247), // map_standard
        D3DCOLOR_XRGB(137, 180, 250), // map_extended
        D3DCOLOR_XRGB(249, 226, 175), // map_selected
    },
    {
        L"Tomorrow night",
        D3DCOLOR_XRGB(29, 31, 33), // background
        {
            D3DCOLOR_XRGB(197, 200, 198), // COLOR_WHITE
            D3DCOLOR_XRGB(204, 102, 102), // COLOR_RED
            D3DCOLOR_XRGB(181, 189, 104), // COLOR_GREEN
            D3DCOLOR_XRGB(129, 162, 190), // COLOR_BLUE
            D3DCOLOR_XRGB(240, 198, 116), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(204, 102, 102), // COLOR_RED_BG
            D3DCOLOR_XRGB(150, 152, 150), // COLOR_GREY
            D3DCOLOR_XRGB(222, 147, 95), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(197, 200, 198), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(222, 147, 95), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(40, 42, 46), // text_background
        D3DCOLOR_ARGB(240, 40, 42, 46), // panel
        D3DCOLOR_XRGB(129, 162, 190), // panel_border
        D3DCOLOR_XRGB(77, 80, 87), // panel_line
        D3DCOLOR_XRGB(55, 59, 65), // selection
        D3DCOLOR_XRGB(40, 42, 46), // button
        D3DCOLOR_XRGB(40, 42, 46), // map_free
        D3DCOLOR_XRGB(129, 162, 190), // map_standard
        D3DCOLOR_XRGB(178, 148, 187), // map_extended
        D3DCOLOR_XRGB(240, 198, 116), // map_selected
    },
    {
        L"Aubergine",
        D3DCOLOR_XRGB(48, 10, 36), // background
        {
            D3DCOLOR_XRGB(238, 238, 236), // COLOR_WHITE
            D3DCOLOR_XRGB(239, 41, 41), // COLOR_RED
            D3DCOLOR_XRGB(138, 226, 52), // COLOR_GREEN
            D3DCOLOR_XRGB(114, 159, 207), // COLOR_BLUE
            D3DCOLOR_XRGB(252, 233, 79), // COLOR_YELLOW
            D3DCOLOR_XRGB(255, 255, 255), // COLOR_WHITE_BG
            D3DCOLOR_XRGB(239, 41, 41), // COLOR_RED_BG
            D3DCOLOR_XRGB(170, 160, 165), // COLOR_GREY
            D3DCOLOR_XRGB(196, 160, 0), // COLOR_DARK_YELLOW
            D3DCOLOR_XRGB(211, 215, 207), // COLOR_LIGHT_GREY
            D3DCOLOR_XRGB(245, 121, 0), // COLOR_WARNING
        },
        D3DCOLOR_XRGB(74, 26, 60), // text_background
        D3DCOLOR_ARGB(240, 44, 0, 30), // panel
        D3DCOLOR_XRGB(233, 84, 32), // panel_border
        D3DCOLOR_XRGB(119, 33, 111), // panel_line
        D3DCOLOR_XRGB(94, 39, 80), // selection
        D3DCOLOR_XRGB(74, 26, 60), // button
        D3DCOLOR_XRGB(74, 26, 60), // map_free
        D3DCOLOR_XRGB(233, 84, 32), // map_standard
        D3DCOLOR_XRGB(173, 127, 168), // map_extended
        D3DCOLOR_XRGB(252, 233, 79), // map_selected
    },
};
const int g_ThemeCount = sizeof(g_Themes) / sizeof(g_Themes[0]);

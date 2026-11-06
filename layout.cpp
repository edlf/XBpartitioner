#include "stdafx.h"
#include "layout.h"

//-----------------------------------------------------------------------------
// Screen layouts (see struct Layout), in screen pixels. 720p uses 1.5x the 480
// values, rounded where needed so things still line up (rows inside their boxes,
// the table centered).
//-----------------------------------------------------------------------------

const Layout g_Layout480 = {
    640, 480, // width, height

    // Main screen
    60,  // margin_x
    20,  // line_height
    52,  // title_x
    16,  // title_y
    400, // loaded_table_x
    430, // free_space_y
    450, // status_y (where the controls hint is, which isn't shown meanwhile)
    450, // prompt_y

    // Partition table, as wide as the system information panel
    30,  // table_left
    610, // table_right
    54,  // table_top
    57,  // table_first_row_y
    5,   // table_header_gap
    20,  // table_row_height
    1,   // table_row_padding (18 + 2x1, +1 to split evenly = 21 bar)
         // Last row 382-403, panel 54-406

    410, // legend_y
    38,  // map_y (between the title and the table)
    10,  // map_height

    // Dialogs
    50,  // dialog_left
    590, // dialog_right
    20,  // dialog_text_inset
    4,   // dialog_title_offset
    50,  // dialog_title_height (32px title + room for the line under it)
    22,  // dialog_line_height
    150, // dialog_button_width
    4,   // dialog_button_padding (18 + 2x4 = 26)
    30,  // dialog_button_gap
    14,  // dialog_padding

    // Menus
    90,  // menu_left
    550, // menu_right
    110, // menu_top
    8,   // menu_title_offset
    37,  // menu_first_item_offset
    3,   // menu_item_padding (18 + 2x3 = 24 bar)
    2,   // menu_item_gap
    20,  // menu_text_inset
    8,   // menu_selection_inset
    30,  // menu_footer_height
    6,   // menu_footer_text_offset

    // System information
    30,  // info_left
    610, // info_right
    170, // info_value_offset

    // Partition edit
    50,  // edit_left
    590, // edit_right

};

const Layout g_Layout720 = {
    960, 720, // width, height

    // Main screen
    90,  // margin_x
    30,  // line_height
    78,  // title_x
    24,  // title_y
    600, // loaded_table_x
    644, // free_space_y
    675, // status_y (where the controls hint is, which isn't shown meanwhile)
    675, // prompt_y

    // Partition table, as wide as the system information panel
    45,  // table_left
    915, // table_right
    81,  // table_top
    86,  // table_first_row_y
    8,   // table_header_gap
    30,  // table_row_height
    1,   // table_row_padding (27 + 2x1, +1 to split evenly = 30 bar)
         // Last row 574-604, panel 81-609

    613, // legend_y
    57,  // map_y (between the title and the table)
    15,  // map_height

    // Dialogs
    75,  // dialog_left
    885, // dialog_right
    30,  // dialog_text_inset
    6,   // dialog_title_offset
    75,  // dialog_title_height (48px title + room for the line under it)
    33,  // dialog_line_height
    225, // dialog_button_width
    6,   // dialog_button_padding (27 + 2x6 = 39)
    45,  // dialog_button_gap
    21,  // dialog_padding

    // Menus
    135, // menu_left
    825, // menu_right
    165, // menu_top
    12,  // menu_title_offset
    56,  // menu_first_item_offset
    4,   // menu_item_padding (27 + 2x4 = 35 bar)
    3,   // menu_item_gap
    30,  // menu_text_inset
    12,  // menu_selection_inset
    45,  // menu_footer_height
    9,   // menu_footer_text_offset

    // System information
    45,  // info_left
    915, // info_right
    255, // info_value_offset

    // Partition edit
    75,  // edit_left
    885, // edit_right

};

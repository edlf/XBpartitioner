#ifndef LAYOUT_H_
#define LAYOUT_H_

// In screen pixels of the 4:3 area, one per video mode (layout.cpp)
struct Layout
{
    int width;  // 4:3 area
    int height;

    // Main screen
    int margin_x; // Left edge of messages and prompts
    int line_height;
    int title_x;
    int title_y;
    int loaded_table_x; // "Current table [...]", right of the title
    int free_space_y;   // "Free: ... of ...", under the legend
    int prompt_y;       // Controls hint

    // Rows ("slots", table_row_height apart): headers, a line, "Standard
    // partitions", 1-5, "Extended partitions", 6-14
    int table_left;
    int table_right;
    int table_top;          // Panel top; the same space is left below the last row
    int table_first_row_y;  // Top of the header row's bar
    int table_header_gap;   // Extra space after the header row, for the line
    int table_row_height;   // Row pitch
    int table_row_padding;  // Above and below the text in a row's selection bar

    // Under the table, right aligned
    int legend_y;
    // Above the table
    int map_y;
    int map_height;

    // Dialog boxes, centered vertically
    int dialog_left;
    int dialog_right;
    int dialog_text_inset;
    int dialog_title_offset; // Title text from the top of the box
    int dialog_title_height;
    int dialog_line_height;
    int dialog_button_width;
    int dialog_button_padding; // Above and below the label: button height = text + 2x this
    int dialog_button_gap;
    int dialog_padding;

    // Menus
    int menu_left;
    int menu_right;
    int menu_top;
    int menu_title_offset;      // Title text from the top of the box
    int menu_first_item_offset; // First item slot from the top of the box
    int menu_item_padding;      // Above and below the text: bar height = text + 2x this
    int menu_item_gap;          // Space between two selection bars
    int menu_text_inset;        // Item text from the left of the box
    int menu_selection_inset;   // Selection bar from the sides of the box
    int menu_footer_height;
    int menu_footer_text_offset;

    // System information: a wider menu panel, rows spaced like menu items
    int info_left;
    int info_right;
    int info_value_offset; // Values from the panel's left edge (labels use menu_text_inset)

    // Partition edit panel: laid out like system information, slightly narrower
    int edit_left;
    int edit_right;
};

extern const Layout g_Layout480; // 640x480: 480i and 480p
extern const Layout g_Layout720; // 960x720 area of 720p

#endif // LAYOUT_H_

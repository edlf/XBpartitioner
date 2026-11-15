#ifndef FATXGUI_PANE_TABLE_HPP_
#define FATXGUI_PANE_TABLE_HPP_

// What the browser's two panes draw alike: the path bar and the list

#include <string>
#include <vector>

#include "theme.h"

namespace fatxgui
{

// A row as shown, made once when the folder is read
struct PaneRow
{
    std::string label; // Folders end in '\'
    bool directory;
    std::string size;
    std::string modified;
};

struct PaneClicks
{
    int clicked = -1; // A row clicked
    int opened = -1;  // A row double-clicked
};

// The rows in a scrolling table of height (0: the rest of the pane).
// selected: one flag per row, or empty when the pane has no selection.
PaneClicks draw_pane_table(const char *id, const std::vector<PaneRow> &rows, const std::vector<bool> &selected, const Theme &theme, float height);

struct PathClicks
{
    int go_back = -1; // Depth in the path to go back to
    bool refresh = false;
};

// Up, Refresh, then each part of the path as a button back to it
PathClicks draw_path_bar(const std::vector<std::string> &parts);

} // namespace fatxgui

#endif // FATXGUI_PANE_TABLE_HPP_

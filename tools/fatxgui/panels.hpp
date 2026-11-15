#ifndef FATXGUI_PANELS_HPP_
#define FATXGUI_PANELS_HPP_

// Everything in the window but the log, and the windows the buttons open

#include <windows.h>

#include "commands.hpp"
#include "file_browser.hpp"
#include "look.hpp"

namespace fatxgui
{

// The selected partition's resize controls
struct ResizeEdit
{
    int part = -1;     // Which partition the value is for
    uint64_t start = 0;
    double mb = 0;
};

struct PanelParts
{
    Commands &commands;
    Workspace &workspace;
    Look &look;
    FileBrowser &browser;
    HWND window;
    float scale;
};

// Everything in the window but the log, and the windows the buttons open
class Panels
{
  public:
    explicit Panels(const PanelParts &parts)
        : m_commands(parts.commands), m_workspace(parts.workspace), m_look(parts.look), m_browser(parts.browser), m_window(parts.window),
          m_scale(parts.scale)
    {
    }

    void Toolbar();
    // A bar with the partitions along the disk; click to select, right-click
    // for the partition's menu
    void Map();
    void Table();
    // Unfinished work, and TRIM on drives
    void Banners();
    // The selected partition: what it is, and what can be done to it
    void Selected();
    // The modal windows, one at a time
    void DriveWindow();
    void FormatDriveWindow();
    void XboxHddWindow();
    void NewImageWindow();
    void ResizeWindow();

  private:
    const Source &Src() const
    {
        return m_workspace.src;
    }
    const Theme &Colors() const
    {
        return m_look.CurrentTheme();
    }
    // Like Start > Theme on the Xbox; the choice is remembered
    void ThemePicker();
    // What can be done to a partition, as its right-click menu offers it
    static bool CanCheck(const Part &part);
    bool CanResize(const Part &part) const;
    void PartMenuItems(int i);
    void PartitionSummary(const Part &part);
    // True if it was interrupted (nothing else can be done then)
    bool InterruptedResize(const Part &part);
    void Actions(const Part &part);
    // True when Resize was pressed (the confirmation is up)
    bool ResizeControls(const Part &part);
    void KeepingSizes(const Part &part, uint64_t new_lba);
    void ReclusterControls(const Part &part);
    // False, with a way to restart as administrator, when not running as one
    bool HasAdminRights();
    void ShowDriveWindow(bool *window);
    void LookAgainButton();
    void DriveList();
    void FormatDriveList();
    void FormatDriveRow(const DriveInfo &drive);
    static std::string drive_text(const DriveInfo &drive);
    void XboxHddOptions();
    void XboxHddRow(const DriveInfo &drive);
    void NewImageForm();
    void XboxHddImageForm();
    void MemoryUnitImageForm();

    Commands &m_commands;
    Workspace &m_workspace;
    Look &m_look;
    FileBrowser &m_browser;
    const HWND m_window;
    const float m_scale;
    ResizeEdit m_resize;
    int m_recluster_choice = 0;
    int m_resize_window = -1; // The partition whose resize window is open (right-click menu)
    NewImage m_new;
    bool m_drive_window = false;
    bool m_format_window = false;
    bool m_xbox_window = false;
    int m_xbox_layout = XBOX_HDD_F; // XboxHddLayout
    bool m_xbox_v4 = true;
};

} // namespace fatxgui

#endif // FATXGUI_PANELS_HPP_

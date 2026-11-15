#ifndef FATXGUI_LOCAL_PANE_HPP_
#define FATXGUI_LOCAL_PANE_HPP_

// The browser's left pane: the PC's drives, then their folders. Double-click
// opens a drive or folder, click selects, Ctrl+click adds more. Copies from the
// right pane land in the folder shown; the selection can go the other way.

#include <filesystem>
#include <string>
#include <vector>

#include "local_folder.hpp"
#include "pane_table.hpp"

namespace fatxgui
{

class LocalPane
{
  public:
    // Fills the space it's given; the list leaves footer_height below it
    void Draw(const Theme &theme, float footer_height);
    // Reads what's shown again, e.g. after a copy into it
    void Refresh();

    // The drive list is shown, not a folder
    bool AtDrives() const
    {
        return m_chain.empty();
    }
    // "C:\Games\Halo"
    std::wstring Folder() const;
    // The selected files and folders (none at the drive list)
    std::vector<std::filesystem::path> SelectedPaths() const;

  private:
    void ReadDrives();
    void ReadFolder();
    void Open(int row);
    void Select(int row);
    void GoBackTo(int depth);
    std::vector<std::string> PathParts() const;

    // The drive, then each folder down to the one shown; empty for the drives
    std::vector<std::wstring> m_chain;
    std::vector<LocalDrive> m_drives;
    std::vector<LocalEntry> m_entries;
    std::vector<PaneRow> m_rows; // Drives or entries, whichever is shown
    std::vector<bool> m_selected; // Alongside m_entries
    std::string m_error;
    bool m_read = false; // Drives are only looked at once the pane is shown
};

} // namespace fatxgui

#endif // FATXGUI_LOCAL_PANE_HPP_

#ifndef FATXGUI_FILE_BROWSER_HPP_
#define FATXGUI_FILE_BROWSER_HPP_

// A two pane file window, commander style: the PC's drives and folders on the
// left, a FATX partition on the right, and copying either way. Copies into the
// partition are checked against the Xbox's limits and asked about first.

#include "commands.hpp"
#include "fatx_pane.hpp"
#include "local_pane.hpp"
#include "theme.h"
#include "workspace.hpp"

namespace fatxgui
{

class FileBrowser
{
  public:
    explicit FileBrowser(Commands &commands) : m_commands(commands)
    {
    }

    // Shows the partition's root folder on the right; the left pane stays
    // where it was
    void Open(const Source &src, const Part &part);
    // Closes itself once the partition is gone or has changed (after a reload)
    void Draw(const Workspace &workspace, const Theme &theme);

  private:
    // Refreshes a pane a finished copy went into
    void CatchUp(const Workspace &workspace);
    void Panes(const Theme &theme);
    // The copy buttons, under the panes
    void CopyBar();
    bool CanCopyToPc() const;
    bool CanCopyToPartition() const;
    void CopyToPc();
    void CopyToPartition();
    // Under the partition's list: new folder, delete, the label
    void EditBar();
    // The box asking for a folder name or a label
    void NameWindow();

    // What the name box is for
    enum NameFor
    {
        NAME_NONE,
        NAME_FOLDER,
        NAME_LABEL
    };

    Commands &m_commands;
    bool m_open = false;
    LocalPane m_local;
    FatxPane m_fatx;
    int m_copies_to_pc_seen = 0; // Workspace's counts when the panes were read
    int m_copies_to_fatx_seen = 0;
    NameFor m_name_for = NAME_NONE;
    char m_name[64] = {};
};

} // namespace fatxgui

#endif // FATXGUI_FILE_BROWSER_HPP_

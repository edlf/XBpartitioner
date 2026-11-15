#ifndef FATXGUI_FATX_PANE_HPP_
#define FATXGUI_FATX_PANE_HPP_

// The browser's right pane: a FATX partition's folders, read only. Click
// selects, Ctrl+click adds more, double-click opens a folder.

#include <string>
#include <vector>

#include "fatx_folder.hpp"
#include "fatx_import.hpp"
#include "pane_table.hpp"
#include "workspace.hpp"

namespace fatxgui
{

class FatxPane
{
  public:
    // Shows the partition's root folder
    void Open(const Source &src, const Part &part);
    // Whether the partition is still there as it was (not after a resize)
    bool IsStillThere(const Source &current) const;

    // Fills the space it's given; the list leaves footer_height below it
    void Draw(const Theme &theme, float footer_height);

    const Source &Src() const
    {
        return m_src;
    }
    const Part &Partition() const
    {
        return m_part;
    }
    // A folder is shown (the partition could be read)
    bool Ready() const
    {
        return m_path.back().cluster != 0;
    }
    int SelectedCount() const;
    // The selected entries, or the folder shown when none is
    std::vector<FatxFolderEntry> ToCopy() const;
    // "3 folders, 12 files, 1.20 GB"
    std::string Summary() const;
    // The folder shown, for copies into it
    FatxImportTarget Here() const;
    // The selected entries only
    std::vector<FatxFolderEntry> Selected() const;
    const std::string &Label() const
    {
        return m_label;
    }
    uint32_t ClusterBytes() const
    {
        return m_cluster_bytes;
    }
    // Reads the folder shown again, e.g. after a copy into it
    void Refresh();

  private:
    // A folder on the way from the root to the one shown
    struct Folder
    {
        uint32_t cluster; // 0 for the root, until it's read
        std::string name;
    };

    void ReadFolder();
    void Select(int entry);
    std::vector<std::string> PathParts() const;

    Source m_src;
    Part m_part;
    std::vector<Folder> m_path;
    std::vector<FatxFolderEntry> m_entries;
    std::vector<PaneRow> m_rows;  // Alongside m_entries
    std::vector<bool> m_selected; // Alongside m_entries
    std::string m_error;
    std::string m_label;
    uint32_t m_cluster_bytes = 0;
};

} // namespace fatxgui

#endif // FATXGUI_FATX_PANE_HPP_

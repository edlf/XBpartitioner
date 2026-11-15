// See fatx_pane.hpp

#define NOMINMAX
#include "fatx_pane.hpp"

#include <algorithm>

#include "activity_log.hpp"
#include "imgui.h"
#include "text.hpp"

namespace fatxgui
{

namespace
{

std::string folder_error(FatxFolderStatus status)
{
    switch (status)
    {
    case FATX_FOLDER_NOT_FATX:
        return "It isn't FATX (anymore).";
    case FATX_FOLDER_RESIZING:
        return "A resize was interrupted: finish it first.";
    case FATX_FOLDER_BAD_HEADER:
        return "The FATX header is damaged: Check shows more.";
    case FATX_FOLDER_BROKEN_CHAIN:
        return "This folder's clusters are damaged, it may be incomplete: Check shows more.";
    default:
        return "It could not be read.";
    }
}

bool lower_char_less(char a, char b)
{
    return tolower((unsigned char)a) < tolower((unsigned char)b);
}

bool lower_less(const std::string &a, const std::string &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), lower_char_less);
}

// Folders first, then by name, ignoring case like the Xbox
bool listed_before(const FatxFolderEntry &a, const FatxFolderEntry &b)
{
    if (a.directory != b.directory)
    {
        return a.directory;
    }
    return lower_less(a.name, b.name);
}

PaneRow row_for(const FatxFolderEntry &entry)
{
    PaneRow row;
    row.label = entry.directory ? entry.name + "\\" : entry.name;
    row.directory = entry.directory;
    row.size = entry.directory ? std::string() : size_text(entry.size);
    row.modified = fatx_time_text(entry.modified_date, entry.modified_time);
    return row;
}

} // namespace

void FatxPane::Open(const Source &src, const Part &part)
{
    m_src = src;
    m_part = part;
    m_path.assign(1, Folder{0, partition_name(part.index)});
    ReadFolder();
}

bool FatxPane::IsStillThere(const Source &current) const
{
    if (current.type != m_src.type || current.path != m_src.path)
    {
        return false;
    }
    for (const Part &part : current.parts)
    {
        if (part.index == m_part.index && part.start == m_part.start && part.sectors == m_part.sectors)
        {
            return part.fits && part.fatx && !part.resizing;
        }
    }
    return false;
}

void FatxPane::ReadFolder()
{
    m_entries.clear();
    m_error.clear();

    SourceIo io;
    if (io.Open(m_src, false, false, &m_error))
    {
        FatxOffsetReader reader(io.Disk(), m_part.start * HDD_SECTOR_SIZE);
        FatxFolderReader folders(reader, m_part.sectors * HDD_SECTOR_SIZE);
        FatxFolderStatus status = folders.Open();
        if (status == FATX_FOLDER_OK)
        {
            if (m_path.back().cluster == 0)
            {
                m_path.back().cluster = folders.RootCluster();
            }
            m_label = folders.Label();
            m_cluster_bytes = m_part.usage.cluster_bytes;
            status = folders.List(m_path.back().cluster, &m_entries);
        }
        if (status != FATX_FOLDER_OK)
        {
            m_error = folder_error(status);
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), listed_before);
    m_rows.clear();
    for (const FatxFolderEntry &entry : m_entries)
    {
        m_rows.push_back(row_for(entry));
    }
    m_selected.assign(m_entries.size(), false);
}

void FatxPane::Draw(const Theme &theme, float footer_height)
{
    const PathClicks path = draw_path_bar(PathParts());
    if (!m_error.empty())
    {
        ImGui::TextColored(tone_color(theme, TONE_ERROR), "%s", m_error.c_str());
    }
    const PaneClicks clicks = draw_pane_table("fatx", m_rows, m_selected, theme, -footer_height);

    // After drawing: reading replaces the rows
    if (clicks.clicked >= 0)
    {
        Select(clicks.clicked);
    }
    if (clicks.opened >= 0 && m_entries[clicks.opened].directory)
    {
        const FatxFolderEntry entry = m_entries[clicks.opened];
        m_path.push_back(Folder{entry.first_cluster, entry.name});
        ReadFolder();
    }
    else if (path.go_back >= 0)
    {
        m_path.resize(path.go_back + 1);
        ReadFolder();
    }
    else if (path.refresh)
    {
        ReadFolder();
    }
}

std::vector<std::string> FatxPane::PathParts() const
{
    std::vector<std::string> parts;
    for (const Folder &folder : m_path)
    {
        parts.push_back(folder.name);
    }
    return parts;
}

void FatxPane::Select(int entry)
{
    if (ImGui::GetIO().KeyCtrl)
    {
        m_selected[entry] = !m_selected[entry];
        return;
    }
    m_selected.assign(m_entries.size(), false);
    m_selected[entry] = true;
}

int FatxPane::SelectedCount() const
{
    return (int)std::count(m_selected.begin(), m_selected.end(), true);
}

std::vector<FatxFolderEntry> FatxPane::Selected() const
{
    std::vector<FatxFolderEntry> chosen;
    for (size_t i = 0; i < m_entries.size(); i++)
    {
        if (m_selected[i])
        {
            chosen.push_back(m_entries[i]);
        }
    }
    return chosen;
}

std::vector<FatxFolderEntry> FatxPane::ToCopy() const
{
    std::vector<FatxFolderEntry> chosen;
    for (size_t i = 0; i < m_entries.size(); i++)
    {
        if (m_selected[i])
        {
            chosen.push_back(m_entries[i]);
        }
    }
    if (chosen.empty())
    {
        FatxFolderEntry shown = {};
        shown.name = m_path.back().name;
        shown.directory = true;
        shown.first_cluster = m_path.back().cluster;
        chosen.push_back(shown);
    }
    return chosen;
}

FatxImportTarget FatxPane::Here() const
{
    // The path from the root, without the partition's own name
    std::string path;
    for (size_t i = 1; i < m_path.size(); i++)
    {
        path += "\\" + m_path[i].name;
    }
    return FatxImportTarget{m_path.back().cluster, path};
}

void FatxPane::Refresh()
{
    ReadFolder();
}

std::string FatxPane::Summary() const
{
    int folders = 0;
    uint64_t bytes = 0;
    for (const FatxFolderEntry &entry : m_entries)
    {
        folders += entry.directory ? 1 : 0;
        bytes += entry.size;
    }
    const int files = (int)m_entries.size() - folders;
    return format("%d folder%s, %d file%s, %s", folders, folders == 1 ? "" : "s", files, files == 1 ? "" : "s", size_text(bytes).c_str());
}

} // namespace fatxgui

// See local_pane.hpp

#define NOMINMAX
#include "local_pane.hpp"

#include "activity_log.hpp"
#include "imgui.h"
#include "text.hpp"

namespace fatxgui
{

namespace
{

const char DRIVES_NAME[] = "This PC";

PaneRow row_for(const LocalDrive &drive)
{
    PaneRow row;
    row.label = local_drive_text(drive);
    row.directory = drive.ready;
    return row;
}

PaneRow row_for(const LocalEntry &entry)
{
    PaneRow row;
    row.label = utf8(entry.name) + (entry.directory ? "\\" : "");
    row.directory = entry.directory;
    row.size = entry.directory ? std::string() : size_text(entry.size);
    row.modified = local_time_text(entry.modified);
    return row;
}

} // namespace

std::wstring LocalPane::Folder() const
{
    std::wstring folder;
    for (const std::wstring &part : m_chain)
    {
        if (!folder.empty() && folder.back() != L'\\')
        {
            folder += L'\\';
        }
        folder += part;
    }
    return folder;
}

void LocalPane::Refresh()
{
    if (AtDrives())
    {
        ReadDrives();
    }
    else
    {
        ReadFolder();
    }
}

void LocalPane::ReadDrives()
{
    m_error.clear();
    m_entries.clear();
    m_drives = list_local_drives();
    m_selected.clear();
    m_rows.clear();
    for (const LocalDrive &drive : m_drives)
    {
        m_rows.push_back(row_for(drive));
    }
}

void LocalPane::ReadFolder()
{
    m_error.clear();
    list_local_folder(Folder(), &m_entries, &m_error);
    m_rows.clear();
    for (const LocalEntry &entry : m_entries)
    {
        m_rows.push_back(row_for(entry));
    }
    m_selected.assign(m_entries.size(), false);
}

void LocalPane::Draw(const Theme &theme, float footer_height)
{
    if (!m_read)
    {
        m_read = true;
        ReadDrives();
    }
    const PathClicks path = draw_path_bar(PathParts());
    if (!m_error.empty())
    {
        ImGui::TextColored(tone_color(theme, TONE_ERROR), "%s", m_error.c_str());
    }
    const PaneClicks clicks = draw_pane_table("local", m_rows, m_selected, theme, -footer_height);

    // After drawing: reading replaces the rows
    if (clicks.clicked >= 0 && !AtDrives())
    {
        Select(clicks.clicked);
    }
    if (clicks.opened >= 0)
    {
        Open(clicks.opened);
    }
    else if (path.go_back >= 0)
    {
        GoBackTo(path.go_back);
    }
    else if (path.refresh)
    {
        Refresh();
    }
}

void LocalPane::Open(int row)
{
    if (AtDrives())
    {
        if (m_drives[row].ready)
        {
            m_chain.assign(1, m_drives[row].root);
            ReadFolder();
        }
        return;
    }
    if (m_entries[row].directory)
    {
        m_chain.push_back(m_entries[row].name);
        ReadFolder();
    }
}

void LocalPane::Select(int row)
{
    if (ImGui::GetIO().KeyCtrl)
    {
        m_selected[row] = !m_selected[row];
        return;
    }
    m_selected.assign(m_entries.size(), false);
    m_selected[row] = true;
}

std::vector<std::filesystem::path> LocalPane::SelectedPaths() const
{
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path folder = Folder();
    for (size_t i = 0; i < m_entries.size(); i++)
    {
        if (m_selected[i])
        {
            paths.push_back(folder / m_entries[i].name);
        }
    }
    return paths;
}

// Depth 0 is the drive list, 1 the drive's root, and so on
void LocalPane::GoBackTo(int depth)
{
    m_chain.resize(depth);
    Refresh();
}

std::vector<std::string> LocalPane::PathParts() const
{
    std::vector<std::string> parts(1, DRIVES_NAME);
    for (const std::wstring &part : m_chain)
    {
        // "C:\" shows as "C:": the path bar puts the backslashes in between
        const bool root = !part.empty() && part.back() == L'\\';
        parts.push_back(utf8(root ? part.substr(0, part.size() - 1) : part));
    }
    return parts;
}

} // namespace fatxgui

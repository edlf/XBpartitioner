// The two pane file window, see file_browser.hpp

#define NOMINMAX
#include "file_browser.hpp"

#include <algorithm>

#include "imgui.h"
#include "text.hpp"

namespace fatxgui
{

void FileBrowser::Open(const Source &src, const Part &part)
{
    m_open = true;
    m_fatx.Open(src, part);
}

void FileBrowser::Draw(const Workspace &workspace, const Theme &theme)
{
    if (!m_open)
    {
        return;
    }
    if (!m_fatx.IsStillThere(workspace.src))
    {
        m_open = false;
        return;
    }
    CatchUp(workspace);

    // Nearly all of the app window, centered, each time it opens
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.94f, viewport->WorkSize.y * 0.86f), ImGuiCond_Appearing);
    const std::string title = "Files: PC and " + partition_name(m_fatx.Partition().index) + "###files";
    // Windows are see-through for the main one's background; this one floats
    // over it, so it gets the dialogs' panel color
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    const bool visible = ImGui::Begin(title.c_str(), &m_open);
    ImGui::PopStyleColor();
    if (visible)
    {
        Panes(theme);
        CopyBar();
    }
    ImGui::End();
}

void FileBrowser::CatchUp(const Workspace &workspace)
{
    if (workspace.copies_to_pc != m_copies_to_pc_seen)
    {
        m_copies_to_pc_seen = workspace.copies_to_pc;
        m_local.Refresh();
    }
    if (workspace.copies_to_fatx != m_copies_to_fatx_seen)
    {
        m_copies_to_fatx_seen = workspace.copies_to_fatx;
        m_fatx.Refresh();
    }
}

// Side by side, each with its own path bar and list
void FileBrowser::Panes(const Theme &theme)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing) / 2;
    const float height = -ImGui::GetFrameHeightWithSpacing(); // Room for the copy bar
    const float summary_height = ImGui::GetTextLineHeightWithSpacing();

    ImGui::BeginChild("pc", ImVec2(width, height));
    m_local.Draw(theme, 0);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("fatx", ImVec2(width, height));
    m_fatx.Draw(theme, ImGui::GetFrameHeightWithSpacing() + summary_height);
    ImGui::TextDisabled("%s", m_fatx.Summary().c_str());
    EditBar();
    ImGui::EndChild();
    NameWindow();
}

bool FileBrowser::CanCopyToPc() const
{
    return m_fatx.Ready() && !m_local.AtDrives();
}

bool FileBrowser::CanCopyToPartition() const
{
    return m_fatx.Ready() && m_fatx.Src().Writable() && !m_local.SelectedPaths().empty();
}

// Both buttons, centered under the panes
void FileBrowser::CopyBar()
{
    const int count = m_fatx.SelectedCount();
    const std::string to_pc = "<< Copy " + (count > 0 ? format("%d selected", count) : std::string("this folder")) + " to the PC";
    const size_t pc_count = m_local.SelectedPaths().size();
    const std::string to_partition = format("Copy %u selected to the partition >>", (unsigned int)pc_count);

    const ImGuiStyle &style = ImGui::GetStyle();
    const float buttons_width = ImGui::CalcTextSize(to_pc.c_str()).x + ImGui::CalcTextSize(to_partition.c_str()).x + 4 * style.FramePadding.x +
                                style.ItemSpacing.x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowContentRegionMax().x - buttons_width) / 2));

    ImGui::BeginDisabled(!CanCopyToPc());
    if (ImGui::Button(to_pc.c_str()))
    {
        CopyToPc();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(m_local.AtDrives() ? "Open a folder on the left first." : "Into the folder on the left. Files already there are never replaced.");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!CanCopyToPartition());
    if (ImGui::Button(to_partition.c_str()))
    {
        CopyToPartition();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Into the folder on the right: select on the left first (Ctrl+click adds more).\n"
                          "Names over 42 characters, paths over 240, files of 4 GB or more and names\n"
                          "already there are skipped. It's checked and asked about before anything is written.");
    }
}

void FileBrowser::EditBar()
{
    const bool ready = m_fatx.Ready() && m_fatx.Src().Writable();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("New folder..."))
    {
        m_name_for = NAME_FOLDER;
        m_name[0] = '\0';
    }
    ImGui::SameLine();
    const std::vector<FatxFolderEntry> selected = m_fatx.Selected();
    ImGui::BeginDisabled(selected.empty());
    if (ImGui::Button("Delete..."))
    {
        m_commands.AskDelete(DeleteRequest{m_fatx.Src(), m_fatx.Partition(), selected, m_fatx.ClusterBytes()});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Label..."))
    {
        m_name_for = NAME_LABEL;
        snprintf(m_name, sizeof(m_name), "%s", m_fatx.Label().c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Label: %s", m_fatx.Label().empty() ? "(none)" : m_fatx.Label().c_str());
}

// Asks for the new folder's name or the label, and says what's wrong with it
void FileBrowser::NameWindow()
{
    if (m_name_for == NAME_NONE)
    {
        return;
    }
    const bool folder = m_name_for == NAME_FOLDER;
    const char *title = folder ? "New folder###name" : "Volume label###name";
    ImGui::OpenPopup(title);
    bool open = true;
    if (!ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        m_name_for = NAME_NONE;
        return;
    }
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(320);
    const bool entered = ImGui::InputText(folder ? "Name" : "Label", m_name, folder ? FATX_MAX_NAME_LENGTH + 1 : FATX_LABEL_LENGTH + 1,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string text = m_name;
    const std::string problem = folder ? (fatx_name_allowed(text) ? std::string() : std::string(FATX_NAME_RULE))
                                       : fatx_label_problem(text);
    if (!problem.empty() && !(folder && text.empty()))
    {
        ImGui::TextDisabled("%s", problem.c_str());
    }
    ImGui::BeginDisabled(!problem.empty());
    const bool ok = ImGui::Button(folder ? "Make" : "Write label") || (entered && problem.empty());
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel") || !open;
    if (ok)
    {
        if (folder)
        {
            m_commands.NewFolder(NewFolderRequest{m_fatx.Src(), m_fatx.Partition(), m_fatx.Here(), text});
        }
        else
        {
            m_commands.SetLabel(LabelRequest{m_fatx.Src(), m_fatx.Partition(), text});
        }
    }
    if (ok || cancel)
    {
        m_name_for = NAME_NONE;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void FileBrowser::CopyToPc()
{
    m_commands.Extract(ExtractRequest{m_fatx.Src(), m_fatx.Partition(), m_fatx.ToCopy(), fs::path(m_local.Folder())});
}

void FileBrowser::CopyToPartition()
{
    m_commands.Import(ImportRequest{m_fatx.Src(), m_fatx.Partition(), m_fatx.Here(), m_local.SelectedPaths()});
}

} // namespace fatxgui

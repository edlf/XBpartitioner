// The windows the buttons open: resize, open a drive, new image

#define NOMINMAX
#include "panels.hpp"

#include <algorithm>

#include "text.hpp"
#include "windows_shell.hpp"

namespace fatxgui
{

// The resize controls in a window, from the right-click menu
void Panels::ResizeWindow()
{
    if (m_resize_window < 0)
    {
        return;
    }
    if (m_resize_window >= (int)Src().parts.size() || !CanResize(Src().parts[m_resize_window]))
    {
        m_resize_window = -1; // Reloaded meanwhile
        return;
    }
    const Part part = Src().parts[m_resize_window];
    m_workspace.selected = m_resize_window;

    const std::string id = "Resize " + partition_name(part.index) + "###resize";
    ImGui::OpenPopup(id.c_str());
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * m_scale, 0));
    bool open = true;
    if (!ImGui::BeginPopupModal(id.c_str(), &open, ImGuiWindowFlags_NoResize))
    {
        m_resize_window = -1;
        return;
    }
    ImGui::TextDisabled("Now %s, %s used, %u KB clusters.",
                        megabytes_text(part.sectors).c_str(),
                        size_text(part.usage.used_clusters * part.usage.cluster_bytes).c_str(),
                        part.usage.cluster_bytes / 1024);
    ImGui::Spacing();
    const bool asked = ResizeControls(part);
    ImGui::Spacing();
    if (ImGui::Button("Close"))
    {
        open = false;
    }
    if (asked || !open)
    {
        m_resize_window = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Panels::DriveWindow()
{
    if (!m_drive_window)
    {
        return;
    }
    ImGui::OpenPopup("Open a drive");
    ImGui::SetNextWindowSize(ImVec2(480 * m_scale, 0));
    if (!ImGui::BeginPopupModal("Open a drive", &m_drive_window, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    if (!is_elevated())
    {
        ImGui::TextWrapped("Reading drives needs administrator rights.");
        if (ImGui::Button("Restart as administrator"))
        {
            restart_elevated(m_window);
        }
    }
    else
    {
        DriveList();
    }
    ImGui::EndPopup();
}

// The Xbox drives found; one picked opens and closes the window
void Panels::DriveList()
{
    ImGui::TextWrapped("Drives with an Xbox partition table, connected directly or by USB. Repairs and resizes are "
                       "written straight to the drive, so back it up first. Some USB adapters don't pass TRIM on.");
    ImGui::Spacing();
    if (m_workspace.drives.empty())
    {
        ImGui::TextDisabled(m_workspace.drives_scanned ? "None found." : "Looking...");
    }
    for (const DriveInfo &drive : m_workspace.drives)
    {
        const std::string label = format("PhysicalDrive%d: %s, %d partitions", drive.number, size_text(drive.bytes).c_str(), drive.partitions);
        if (ImGui::Selectable(label.c_str()))
        {
            m_commands.OpenDrive(drive.number);
            m_drive_window = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Look again"))
    {
        m_workspace.drives.clear();
        m_workspace.drives_scanned = false;
        m_commands.ScanDrives();
    }
}

void Panels::NewImageWindow()
{
    if (!m_new.open)
    {
        return;
    }
    ImGui::OpenPopup("New image");
    ImGui::SetNextWindowSize(ImVec2(560 * m_scale, 0));
    if (!ImGui::BeginPopupModal("New image", &m_new.open, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    NewImageForm();

    ImGui::Spacing();
    ImGui::BeginDisabled(m_new.path.empty());
    if (ImGui::Button("Create"))
    {
        const NewImage spec = m_new;
        m_new.open = false;
        ImGui::CloseCurrentPopup();
        m_commands.Make(spec);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        m_new.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The file, size, cluster size and folder to fill it from
void Panels::NewImageForm()
{
    ImGui::TextWrapped("A formatted FATX partition image, empty or filled from a folder.");
    ImGui::Spacing();
    if (ImGui::Button("File..."))
    {
        pick_file(m_window, true, &m_new.path);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(m_new.path.empty() ? "(none)" : utf8(m_new.path).c_str());

    ImGui::SetNextItemWidth(160 * m_scale);
    ImGui::InputDouble("Size (MB)", &m_new.size_mb, 64.0, 1024.0, "%.0f");
    m_new.size_mb = std::max(1.0, m_new.size_mb);
    static const char *cluster_labels[] = {"Automatic", "16 KB", "32 KB", "64 KB", "128 KB", "256 KB", "512 KB", "1024 KB"};
    ImGui::SetNextItemWidth(160 * m_scale);
    ImGui::Combo("Clusters", &m_new.cluster, cluster_labels, IM_ARRAYSIZE(cluster_labels));

    if (ImGui::Button("Fill from folder..."))
    {
        pick_folder(m_window, &m_new.from);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(m_new.from.empty() ? "(empty)" : utf8(m_new.from).c_str());
    if (!m_new.from.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
        {
            m_new.from.clear();
        }
    }
}

} // namespace fatxgui

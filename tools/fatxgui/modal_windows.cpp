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
    ImGui::SetNextWindowSize(ImVec2(520 * m_scale, 0));
    if (!ImGui::BeginPopupModal("Open a drive", &m_drive_window, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    if (HasAdminRights())
    {
        DriveList();
    }
    ImGui::EndPopup();
}

void Panels::FormatDriveWindow()
{
    if (!m_format_window)
    {
        return;
    }
    ImGui::OpenPopup("Format a drive as FATX");
    ImGui::SetNextWindowSize(ImVec2(560 * m_scale, 0));
    if (!ImGui::BeginPopupModal("Format a drive as FATX", &m_format_window, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    if (HasAdminRights())
    {
        FormatDriveList();
    }
    ImGui::EndPopup();
}

// Says so, with a way to get them, when it isn't running as administrator
bool Panels::HasAdminRights()
{
    if (is_elevated())
    {
        return true;
    }
    ImGui::TextWrapped("Reading drives needs administrator rights.");
    if (ImGui::Button("Restart as administrator"))
    {
        restart_elevated(m_window);
    }
    return false;
}

// Opens the drive window and looks at the drives the first time
void Panels::ShowDriveWindow(bool *window)
{
    *window = true;
    if (is_elevated() && !m_workspace.drives_scanned)
    {
        m_commands.ScanDrives();
    }
}

void Panels::LookAgainButton()
{
    ImGui::Spacing();
    if (ImGui::Button("Look again"))
    {
        m_commands.ForgetDrives();
        m_commands.ScanDrives();
    }
}

// The Xbox and FATX drives found; one picked opens and closes the window
void Panels::DriveList()
{
    ImGui::TextWrapped("Drives with an Xbox partition table or a FATX volume (like a memory unit), connected directly or "
                       "by USB. Changes are written straight to the drive, so back it up first. Some USB adapters don't pass TRIM on.");
    ImGui::Spacing();
    int shown = 0;
    ImGui::PushID("drives"); // The rows' labels can repeat (same model)
    for (const DriveInfo &drive : m_workspace.drives)
    {
        if (drive.content == DRIVE_OTHER)
        {
            continue;
        }
        shown++;
        ImGui::PushID(drive.number);
        const std::string what = drive.content == DRIVE_XBOX_TABLE ? format("%d partitions", drive.partitions) : std::string("FATX, no partition table");
        if (ImGui::Selectable((drive_text(drive) + ", " + what).c_str()))
        {
            m_commands.OpenDrive(drive.number);
            m_drive_window = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    if (shown == 0)
    {
        ImGui::TextDisabled(m_workspace.drives_scanned ? "None found." : "Looking...");
    }
    LookAgainButton();
}

// Every drive but Xbox hard drives (those have their own window), FATX
// sticks included; format_refusal() says which may be formatted
void Panels::FormatDriveList()
{
    ImGui::TextWrapped("Makes a whole drive one FATX volume with no partition table, the layout of a memory unit. "
                       "Everything on it is erased, FATX already on it included. Only removable USB drives up to 4 GB, "
                       "with no volume Windows has mounted.");
    ImGui::Spacing();
    int shown = 0;
    ImGui::PushID("drives");
    for (const DriveInfo &drive : m_workspace.drives)
    {
        if (drive.content == DRIVE_XBOX_TABLE)
        {
            continue;
        }
        shown++;
        ImGui::PushID(drive.number);
        FormatDriveRow(drive);
        ImGui::PopID();
    }
    ImGui::PopID();
    if (shown == 0)
    {
        ImGui::TextDisabled(m_workspace.drives_scanned ? "None found." : "Looking...");
    }
    LookAgainButton();
}

void Panels::FormatDriveRow(const DriveInfo &drive)
{
    ImGui::BeginDisabled(!can_format(drive));
    if (ImGui::SmallButton("Format..."))
    {
        m_format_window = false;
        ImGui::CloseCurrentPopup();
        m_commands.AskFormatDrive(drive);
    }
    ImGui::EndDisabled();
    const std::string refusal = format_refusal(drive);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !refusal.empty())
    {
        ImGui::SetTooltip("%s", refusal.c_str());
    }
    ImGui::SameLine();
    const std::string holds = drive.content == DRIVE_FATX ? ", FATX" : (drive.mounted_volumes.empty() ? "" : ", mounted: " + drive.mounted_volumes);
    ImGui::TextDisabled("%s%s", drive_text(drive).c_str(), holds.c_str());
}

void Panels::XboxHddWindow()
{
    if (!m_xbox_window)
    {
        return;
    }
    ImGui::OpenPopup("Format a drive as an Xbox hard drive");
    ImGui::SetNextWindowSize(ImVec2(620 * m_scale, 0));
    if (!ImGui::BeginPopupModal("Format a drive as an Xbox hard drive", &m_xbox_window, ImGuiWindowFlags_NoResize))
    {
        return;
    }
    if (HasAdminRights())
    {
        ImGui::TextWrapped("Writes an Xbox partition table and formats every partition with FATX, like XBpartitioner. "
                           "Everything on the drive is erased. Drives with a volume Windows has mounted are never formatted here. "
                           "C is left empty: put a dashboard on it before the Xbox can boot from the drive.");
        ImGui::Spacing();
        XboxHddOptions();
        ImGui::Spacing();
        int shown = 0;
        ImGui::PushID("drives");
        for (const DriveInfo &drive : m_workspace.drives)
        {
            shown++;
            ImGui::PushID(drive.number);
            XboxHddRow(drive);
            ImGui::PopID();
        }
        ImGui::PopID();
        if (shown == 0)
        {
            ImGui::TextDisabled(m_workspace.drives_scanned ? "None found." : "Looking...");
        }
        LookAgainButton();
    }
    ImGui::EndPopup();
}

// The layout and the table's LBA48 version
void Panels::XboxHddOptions()
{
    static const char *layouts[] = {"Stock (E, C, X, Y, Z)", "Stock + F (the rest of the drive)", "Stock + F and G (the rest split)"};
    ImGui::SetNextItemWidth(300 * m_scale);
    ImGui::Combo("Layout", &m_xbox_layout, layouts, IM_ARRAYSIZE(layouts));
    ImGui::Checkbox("LBA48 v4 table (over 2 TB, partitions over 1 TB; needs a v4 BIOS such as Cerbios)", &m_xbox_v4);
}

void Panels::XboxHddRow(const DriveInfo &drive)
{
    const std::string refusal = xbox_format_refusal(drive);
    ImGui::BeginDisabled(!refusal.empty());
    if (ImGui::SmallButton("Format..."))
    {
        m_xbox_window = false;
        ImGui::CloseCurrentPopup();
        const XboxHddSpec spec = {(XboxHddLayout)m_xbox_layout, m_xbox_v4, (uint32_t)(GetTickCount64() * 2654435761u)};
        m_commands.AskFormatXboxHdd(drive, spec);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !refusal.empty())
    {
        ImGui::SetTooltip("%s", refusal.c_str());
    }
    ImGui::SameLine();
    const char *holds = drive.content == DRIVE_XBOX_TABLE ? ", Xbox partitions" : (drive.content == DRIVE_FATX ? ", FATX" : "");
    ImGui::TextDisabled("%s%s%s", drive_text(drive).c_str(), holds, drive.mounted_volumes.empty() ? "" : (", mounted: " + drive.mounted_volumes).c_str());
}

// "PhysicalDrive3: Kingston DataTraveler, 14.92 GB"
std::string Panels::drive_text(const DriveInfo &drive)
{
    return format("PhysicalDrive%d: %s%s", drive.number, drive.model.empty() ? "" : (drive.model + ", ").c_str(), size_text(drive.bytes).c_str());
}

void Panels::NewImageWindow()
{
    if (!m_new.open)
    {
        return;
    }
    ImGui::OpenPopup("Create image");
    ImGui::SetNextWindowSize(ImVec2(620 * m_scale, 0));
    if (!ImGui::BeginPopupModal("Create image", &m_new.open, ImGuiWindowFlags_NoResize))
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

// What kind, the file, then that kind's choices
void Panels::NewImageForm()
{
    ImGui::RadioButton("Xbox hard drive", &m_new.kind, NEW_IMAGE_XBOX_HDD);
    ImGui::SameLine();
    ImGui::RadioButton("Memory unit", &m_new.kind, NEW_IMAGE_MEMORY_UNIT);
    ImGui::Spacing();
    if (ImGui::Button("File..."))
    {
        pick_file(m_window, true, &m_new.path);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(m_new.path.empty() ? "(none)" : utf8(m_new.path).c_str());
    ImGui::Spacing();
    if (m_new.kind == NEW_IMAGE_XBOX_HDD)
    {
        XboxHddImageForm();
    }
    else
    {
        MemoryUnitImageForm();
    }
}

void Panels::XboxHddImageForm()
{
    ImGui::TextWrapped("A whole hard drive: an Xbox partition table and an empty FATX file system on every partition, "
                       "like Format Xbox HDD. The file only takes the space that's written.");
    const double min_gb = (double)XBOX_HDD_MIN_BYTES / (1024.0 * 1024.0 * 1024.0);
    ImGui::SetNextItemWidth(160 * m_scale);
    ImGui::InputDouble("Size (GB)", &m_new.hdd_gb, 1.0, 100.0, "%.1f");
    m_new.hdd_gb = std::max(min_gb, std::min(m_new.hdd_gb, 16.0 * 1024.0));
    static const char *layouts[] = {"Stock (E, C, X, Y, Z)", "Stock + F (the rest of the drive)", "Stock + F and G (the rest split)"};
    ImGui::SetNextItemWidth(300 * m_scale);
    ImGui::Combo("Layout", &m_new.layout, layouts, IM_ARRAYSIZE(layouts));
    ImGui::Checkbox("LBA48 v4 table (needs a v4 BIOS such as Cerbios)", &m_new.lba48_v4);
}

void Panels::MemoryUnitImageForm()
{
    ImGui::TextWrapped("One FATX volume with 16 KB clusters and no partition table, as the Xbox formats a memory unit. "
                       "A real memory unit holds 8 MB; USB sticks used as one go up to 4 GB here.");
    ImGui::SetNextItemWidth(160 * m_scale);
    ImGui::InputDouble("Size (MB)", &m_new.mu_mb, 8.0, 512.0, "%.0f");
    m_new.mu_mb = std::max(1.0, std::min(m_new.mu_mb, (double)(FORMAT_MAX_BYTES / (1024 * 1024))));
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

// The toolbar, the disk map, the partition table and its menus, and the banners

#define NOMINMAX
#include "panels.hpp"

#include <algorithm>

#include "text.hpp"
#include "windows_shell.hpp"

namespace fatxgui
{

namespace
{

// The disk map's colors, like on the Xbox
ImU32 part_color(const Theme &theme, int index, bool selected)
{
    if (selected)
    {
        return LookColorU32(theme.map_selected);
    }
    return LookColorU32(index >= 0 && index < XBOX_STANDARD_PARTITION_COUNT ? theme.map_standard : theme.map_extended);
}

std::string theme_name(int index)
{
    return utf8(Look::ThemeAt(index).name);
}

std::string part_state(const Part &part)
{
    if (!part.fits)
    {
        return "past the end";
    }
    if (part.resizing)
    {
        return "interrupted resize";
    }
    if (!part.fatx)
    {
        return "not FATX";
    }
    if (!part.usage_ok)
    {
        return "FATX (unreadable FAT)";
    }
    return format("FATX, %u KB clusters", part.usage.cluster_bytes / 1024);
}

} // namespace

bool Panels::CanCheck(const Part &part)
{
    return part.fits && part.fatx && !part.resizing;
}

bool Panels::CanResize(const Part &part) const
{
    return Src().Writable() && Src().CanResize() && CanCheck(part) && part.usage_ok;
}

void Panels::Toolbar()
{
    if (ImGui::Button("Open image..."))
    {
        std::wstring path;
        if (pick_file(m_window, false, &path))
        {
            m_commands.OpenImage(path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open drive..."))
    {
        ShowDriveWindow(&m_drive_window);
    }
    ImGui::SameLine();
    if (ImGui::Button("Format drive..."))
    {
        ShowDriveWindow(&m_format_window);
    }
    ImGui::SameLine();
    if (ImGui::Button("Format Xbox HDD..."))
    {
        ShowDriveWindow(&m_xbox_window);
    }
    ImGui::SameLine();
    if (ImGui::Button("Create image..."))
    {
        m_new = NewImage();
        m_new.open = true;
    }
    if (Src().type != Source::NONE)
    {
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
        {
            m_commands.Reload();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Src().Kind().c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(Src().name.c_str());
    }
    ThemePicker();
}

void Panels::ThemePicker()
{
    const float width = 150 * m_scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##theme", theme_name(m_look.ThemeIndex()).c_str()))
    {
        for (int i = 0; i < Look::ThemeCount(); i++)
        {
            if (ImGui::Selectable(theme_name(i).c_str(), i == m_look.ThemeIndex()))
            {
                m_look.SetTheme(i);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Theme");
    }
}

void Panels::Map()
{
    const float height = 34.0f * m_scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), LookColorU32(Colors().map_free), 4.0f);

    const double total = (double)(Src().bytes / HDD_SECTOR_SIZE);
    for (int i = 0; i < (int)Src().parts.size(); i++)
    {
        const Part &part = Src().parts[i];
        if (total <= 0)
        {
            break;
        }
        float x0 = origin.x + (float)(part.start / total * width);
        float x1 = origin.x + (float)((part.start + part.sectors) / total * width);
        x1 = std::max(x1, x0 + 3.0f);
        x1 = std::min(x1, origin.x + width);
        const bool selected = i == m_workspace.selected;
        draw->AddRectFilled(ImVec2(x0, origin.y + 2), ImVec2(x1 - 1, origin.y + height - 2), part_color(Colors(), part.index, selected), 3.0f);
        const std::string label = part.index < 0 ? std::string("FATX") : std::string(1, partition_letter(part.index));
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        if (x1 - x0 > size.x + 6)
        {
            // Dark on the bright selected color, light on the others
            const D3DCOLOR text = selected ? Colors().background : Colors().text[COLOR_WHITE];
            draw->AddText(ImVec2((x0 + x1 - size.x) / 2, origin.y + (height - size.y) / 2), LookColorU32(text), label.c_str());
        }
        if (ImGui::IsMouseHoveringRect(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height)))
        {
            ImGui::SetTooltip("%s\n%s", partition_name(part.index).c_str(), size_text(part.sectors * HDD_SECTOR_SIZE).c_str());
            if (ImGui::IsMouseClicked(0))
            {
                m_workspace.selected = i;
            }
            if (ImGui::IsMouseClicked(1))
            {
                m_workspace.selected = i;
                ImGui::OpenPopup("map_menu");
            }
        }
    }
    ImGui::Dummy(ImVec2(width, height));
    if (ImGui::BeginPopup("map_menu"))
    {
        PartMenuItems(m_workspace.selected);
        ImGui::EndPopup();
    }
}

void Panels::Table()
{
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("parts", 5, flags))
    {
        return;
    }
    ImGui::TableSetupColumn("Partition", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("File system", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    // The headers push their column numbers: rows numbered the same would
    // clash with them (header "Partition" and a partition image's row)
    ImGui::PushID("rows");
    for (int i = 0; i < (int)Src().parts.size(); i++)
    {
        const Part &part = Src().parts[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(i);
        if (ImGui::Selectable(partition_name(part.index).c_str(), i == m_workspace.selected, ImGuiSelectableFlags_SpanAllColumns))
        {
            m_workspace.selected = i;
        }
        // Right-click: the partition's menu (and it becomes the selected one)
        if (ImGui::BeginPopupContextItem("part_menu"))
        {
            m_workspace.selected = i;
            PartMenuItems(i);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(size_text(part.start * HDD_SECTOR_SIZE).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(size_text(part.sectors * HDD_SECTOR_SIZE).c_str());
        ImGui::TableNextColumn();
        const std::string state = part_state(part);
        if (part.resizing || !part.fits)
        {
            ImGui::TextColored(tone_color(Colors(), TONE_ERROR), "%s", state.c_str());
        }
        else
        {
            ImGui::TextUnformatted(state.c_str());
        }
        ImGui::TableNextColumn();
        if (part.usage_ok)
        {
            ImGui::TextUnformatted(size_text(part.usage.used_clusters * part.usage.cluster_bytes).c_str());
        }
    }
    ImGui::PopID();
    ImGui::EndTable();
}

void Panels::PartMenuItems(int i)
{
    if (i < 0 || i >= (int)Src().parts.size())
    {
        return;
    }
    const Part part = Src().parts[i];
    ImGui::TextDisabled("%s", partition_name(part.index).c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Check", nullptr, false, CanCheck(part)))
    {
        m_commands.Check(part);
    }
    if (ImGui::MenuItem("Browse files", nullptr, false, CanCheck(part)))
    {
        m_browser.Open(Src(), part);
    }
    if (Src().Writable() && ImGui::MenuItem("Repair...", nullptr, false, CanCheck(part)))
    {
        m_commands.Repair(part);
    }
    if (Src().Writable() && ImGui::MenuItem("Resize...", nullptr, false, CanResize(part)))
    {
        m_resize_window = i;
    }
    if (Src().CanRecluster() && ImGui::MenuItem("Defragment...", nullptr, false, CanResize(part) && !Src().recluster_pending))
    {
        m_commands.Recluster(part, part.usage.cluster_bytes);
    }
    if (part.resizing && Src().Writable() && Src().CanResize() && ImGui::MenuItem("Finish resize"))
    {
        m_commands.ResumeResize(part);
    }
}

void Panels::Banners()
{
    if (Src().recluster_pending)
    {
        ImGui::TextColored(
            tone_color(Colors(), TONE_ERROR), "A change to %u KB clusters didn't finish: the partition is unusable until it does.", Src().plan.new_cluster_bytes / 1024);
        ImGui::SameLine();
        if (ImGui::Button("Finish it"))
        {
            m_commands.ResumeRecluster();
        }
    }
    if (Src().type != Source::DRIVE)
    {
        return;
    }
    const TrimPlan &trim = m_workspace.trim;
    if (!trim.ready)
    {
        if (ImGui::Button("Find free space to TRIM"))
        {
            m_commands.FindTrim();
        }
        return;
    }
    const bool can = trim.identified && trim.supported && !trim.ranges.empty();
    ImGui::Text("Free to trim: %s in %u ranges.", size_text(trim.bytes).c_str(), (unsigned)trim.ranges.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("TRIM now..."))
    {
        m_commands.AskTrim();
    }
    ImGui::EndDisabled();
}

} // namespace fatxgui

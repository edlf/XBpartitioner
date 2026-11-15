// See pane_table.hpp

#define NOMINMAX
#include "pane_table.hpp"

#include "activity_log.hpp"
#include "imgui.h"

namespace fatxgui
{

namespace
{

void draw_row(int i, const PaneRow &row, bool selected, const Theme &theme, PaneClicks *clicks)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(i);
    if (row.directory)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, tone_color(theme, TONE_TITLE));
    }
    if (ImGui::Selectable(row.label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
    {
        clicks->clicked = i;
    }
    if (row.directory)
    {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        clicks->opened = i;
    }
    ImGui::PopID();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.size.c_str());
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", row.modified.c_str());
}

} // namespace

PaneClicks draw_pane_table(const char *id, const std::vector<PaneRow> &rows, const std::vector<bool> &selected, const Theme &theme, float height)
{
    PaneClicks clicks;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable(id, 3, flags, ImVec2(0, height)))
    {
        return clicks;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableHeadersRow();

    // Apart from the headers' column numbers, which a row with the same
    // number and name (a file called "Name") would clash with
    ImGui::PushID("rows");
    ImGuiListClipper clipper;
    clipper.Begin((int)rows.size());
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            draw_row(i, rows[i], !selected.empty() && selected[i], theme, &clicks);
        }
    }
    ImGui::PopID();
    ImGui::EndTable();
    return clicks;
}

PathClicks draw_path_bar(const std::vector<std::string> &parts)
{
    PathClicks clicks;
    ImGui::BeginDisabled(parts.size() < 2);
    if (ImGui::Button("Up"))
    {
        clicks.go_back = (int)parts.size() - 2;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    clicks.refresh = ImGui::Button("Refresh");
    for (size_t i = 0; i < parts.size(); i++)
    {
        ImGui::SameLine();
        ImGui::PushID((int)i);
        if (i + 1 == parts.size())
        {
            ImGui::TextUnformatted(parts[i].c_str());
        }
        else if (ImGui::SmallButton(parts[i].c_str()))
        {
            clicks.go_back = (int)i;
        }
        ImGui::PopID();
        if (i + 1 < parts.size())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("\\");
        }
    }
    return clicks;
}

} // namespace fatxgui

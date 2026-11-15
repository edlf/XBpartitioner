#define NOMINMAX
#include "dialogs.hpp"

#include "imgui.h"

namespace fatxgui
{

namespace
{

// The last lines of a confirmation before writing
std::vector<std::string> write_warning(const Source &src)
{
    if (src.type == Source::DRIVE)
    {
        return {"This writes to the drive itself, not a copy: back it up first.", "Close FATXplorer or anything else that has the drive open."};
    }
    return {"Make sure nothing else has the image open."};
}

} // namespace

void Dialogs::Ask(DialogText text, std::unique_ptr<DialogAction> action)
{
    m_open = true;
    m_text = std::move(text);
    m_text.second_button.clear();
    m_action = std::move(action);
    m_second_action.reset();
}

void Dialogs::Choose(DialogText text, std::unique_ptr<DialogAction> first, std::unique_ptr<DialogAction> second)
{
    m_open = true;
    m_text = std::move(text);
    m_action = std::move(first);
    m_second_action = std::move(second);
}

void Dialogs::Tell(const std::string &title, std::vector<std::string> lines)
{
    Ask(DialogText{title, std::move(lines), std::string()}, nullptr);
}

void Dialogs::Answer(int button)
{
    m_open = false;
    std::unique_ptr<DialogAction> first = std::move(m_action);
    std::unique_ptr<DialogAction> second = std::move(m_second_action);
    DialogAction *chosen = (button == 1) ? first.get() : (button == 2) ? second.get() : nullptr;
    if (chosen != nullptr)
    {
        chosen->Run();
    }
}

void Dialogs::Draw(float scale)
{
    if (!m_open)
    {
        return;
    }
    const std::string id = m_text.title + "###confirm";
    ImGui::OpenPopup(id.c_str());
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    for (const std::string &line : m_text.lines)
    {
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::Spacing();
    bool close = false;
    int chosen = 0;
    if (m_text.button.empty())
    {
        close = ImGui::Button("OK", ImVec2(100 * scale, 0));
    }
    else
    {
        chosen = ImGui::Button(m_text.button.c_str(), ImVec2(0, 0)) ? 1 : 0;
        if (!m_text.second_button.empty())
        {
            ImGui::SameLine();
            chosen = ImGui::Button(m_text.second_button.c_str(), ImVec2(0, 0)) ? 2 : chosen;
        }
        ImGui::SameLine();
        close = ImGui::Button("Cancel", ImVec2(100 * scale, 0)) || chosen != 0;
    }
    if (close)
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    if (close)
    {
        Answer(chosen);
    }
}

std::vector<std::string> with_warning(std::vector<std::string> lines, const Source &src)
{
    for (const std::string &line : write_warning(src))
    {
        lines.push_back(line);
    }
    return lines;
}

} // namespace fatxgui

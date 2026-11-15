// The UI: open a disk image, a partition image or an Xbox drive, see its
// partitions and run fatxtool's commands on them. Slow work runs on a worker
// thread (Job) behind a progress window; nothing else can start meanwhile.
//
// Like fatxtool, only images are ever written. Drives are read, and TRIMmed.
//
// No globals: the Gui object (created by main.cpp) owns the log, the job
// runner, the dialogs, what's open (Workspace), the commands and the panels,
// and hands each the others it needs.

#define NOMINMAX
#include "gui.hpp"

#include <shellapi.h>

#include "imgui.h"
#include "look.hpp"
#include "panels.hpp"
#include "text.hpp"

using namespace fatxgui;

namespace
{

void setup_style(float scale)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.ScaleAllSizes(scale);

    ImGuiIO &io = ImGui::GetIO();
    wchar_t fonts[MAX_PATH];
    GetWindowsDirectoryW(fonts, MAX_PATH);
    const std::string font = utf8(fonts) + "\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        io.Fonts->AddFontFromFileTTF(font.c_str(), 17.0f * scale);
    }
    else
    {
        ImFontConfig config;
        config.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&config);
    }
}

float scale_for(const GuiWindow &window)
{
    return window.dpi_scale > 0 ? window.dpi_scale : 1.0f;
}

} // namespace

struct Gui::Parts
{
    explicit Parts(const GuiWindow &window)
        : scale(scale_for(window)), look(window.hwnd, window.device, window.context), jobs(window.hwnd),
          commands(CommandParts{log, jobs, dialogs, workspace}), panels(PanelParts{commands, workspace, look, window.hwnd, scale})
    {
    }

    const float scale;
    Look look;
    ActivityLog log;
    JobRunner jobs;
    Dialogs dialogs;
    Workspace workspace;
    Commands commands;
    Panels panels;
};

Gui::Gui(const GuiWindow &window)
{
    setup_style(scale_for(window));
    m_parts = std::make_unique<Parts>(window); // Its Look applies the theme's colors, over the sizes
    m_parts->log.Add(TONE_DIM, "Open a disk image, a partition image or an Xbox drive. Images are changed in place: work on a copy.");

    // An image on the command line opens at once
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr)
    {
        if (argc >= 2)
        {
            m_parts->commands.OpenImage(argv[1]);
        }
        LocalFree(argv);
    }
}

// Parts: the job runner waits for a running job
Gui::~Gui() = default;

void Gui::Frame()
{
    Parts &parts = *m_parts;
    parts.look.DrawBackground();

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    parts.panels.Toolbar();
    ImGui::Separator();

    const float log_height = ImGui::GetContentRegionAvail().y * 0.32f;
    // See-through, so the background shows; the table and the log get panels
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("top", ImVec2(0, -log_height - ImGui::GetStyle().ItemSpacing.y));
    ImGui::PopStyleColor();
    if (parts.workspace.src.type == Source::NONE)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Nothing open. Drop an image on the window, or use the buttons above.");
    }
    else
    {
        parts.panels.Map();
        parts.panels.Table();
        ImGui::Spacing();
        parts.panels.Banners();
        ImGui::Separator();
        parts.panels.Selected();
    }
    ImGui::EndChild();

    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    parts.log.Draw(parts.look.CurrentTheme());
    ImGui::EndChild();

    // One modal at a time: ImGui would swap them every frame
    if (!parts.jobs.Busy())
    {
        parts.panels.DriveWindow();
        parts.panels.NewImageWindow();
        parts.panels.ResizeWindow();
        parts.dialogs.Draw(parts.scale);
    }
    parts.jobs.Draw(parts.scale, parts.look.CurrentTheme());

    ImGui::End();
}

bool Gui::CanClose()
{
    return m_parts->jobs.CanClose();
}

void Gui::DropFile(const wchar_t *path)
{
    if (m_parts->jobs.Busy())
    {
        return;
    }
    m_parts->commands.OpenImage(path);
}

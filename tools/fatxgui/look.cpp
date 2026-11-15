// Themes and the moving background from the Xbox app (src/themes.cpp,
// src/background.cpp). The background goes through the same dithered
// gradient texture, sampled exactly (point, wrapping down), so it shows no
// banding here either.

#define NOMINMAX
#include "look.hpp"

#include <dwmapi.h>

#include "background.h"
#include "themes.h"

#include <stdio.h>
#include <string.h>

#pragma comment(lib, "dwmapi.lib")

namespace
{

// Like the Xbox: a long gap (a dragged window) counts as a short step
const float MAX_STEP_SECONDS = 0.25f;

float channel(D3DCOLOR color, int shift)
{
    return ((color >> shift) & 0xFF) / 255.0f;
}

// fatxgui.ini next to the exe: theme=Name
void settings_path(wchar_t *path, size_t count)
{
    GetModuleFileNameW(nullptr, path, (DWORD)count);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash != nullptr)
    {
        slash[1] = L'\0';
    }
    wcsncat_s(path, count, L"fatxgui.ini", _TRUNCATE);
}

void save_theme(int theme)
{
    wchar_t path[MAX_PATH];
    settings_path(path, MAX_PATH);
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"w") != 0 || file == nullptr)
    {
        return; // A read-only folder: not remembered
    }
    fprintf(file, "theme=%ls\n", g_Themes[theme].name);
    fclose(file);
}

int load_theme()
{
    wchar_t path[MAX_PATH];
    settings_path(path, MAX_PATH);
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"r") != 0 || file == nullptr)
    {
        return 0;
    }
    int found = 0;
    char line[160];
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (_strnicmp(line, "theme=", 6) != 0)
        {
            continue;
        }
        wchar_t name[80];
        MultiByteToWideChar(CP_UTF8, 0, line + 6, -1, name, 80);
        for (int i = 0; i < g_ThemeCount; i++)
        {
            if (_wcsicmp(g_Themes[i].name, name) == 0)
            {
                found = i;
            }
        }
    }
    fclose(file);
    return found;
}

ImVec4 mix(D3DCOLOR a, D3DCOLOR b, float amount, float alpha = 1.0f)
{
    const ImVec4 x = LookColor(a);
    const ImVec4 y = LookColor(b);
    return ImVec4(x.x + (y.x - x.x) * amount, x.y + (y.y - x.y) * amount, x.z + (y.z - x.z) * amount, alpha);
}

} // namespace

ImVec4 LookColor(D3DCOLOR color, float alpha)
{
    return ImVec4(channel(color, 16), channel(color, 8), channel(color, 0), alpha >= 0.0f ? alpha : channel(color, 24));
}

ImU32 LookColorU32(D3DCOLOR color, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(LookColor(color, alpha));
}

void Look::ApplyStyle() const
{
    const Theme &t = g_Themes[m_theme];
    ImVec4 *c = ImGui::GetStyle().Colors;

    c[ImGuiCol_Text] = LookColor(t.text[COLOR_WHITE]);
    c[ImGuiCol_TextDisabled] = LookColor(t.text[COLOR_GREY]);
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); // The background shows
    c[ImGuiCol_ChildBg] = LookColor(t.panel, 0.80f);
    c[ImGuiCol_PopupBg] = LookColor(t.panel, 0.97f);
    c[ImGuiCol_Border] = LookColor(t.panel_border, 0.75f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = LookColor(t.button);
    c[ImGuiCol_FrameBgHovered] = mix(t.button, t.selection, 0.5f);
    c[ImGuiCol_FrameBgActive] = LookColor(t.selection);
    c[ImGuiCol_TitleBg] = LookColor(t.panel_line);
    c[ImGuiCol_TitleBgActive] = LookColor(t.selection);
    c[ImGuiCol_TitleBgCollapsed] = LookColor(t.panel_line);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = LookColor(t.button);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(t.button, t.selection, 0.5f);
    c[ImGuiCol_ScrollbarGrabActive] = LookColor(t.selection);
    c[ImGuiCol_CheckMark] = LookColor(t.text[COLOR_YELLOW]);
    c[ImGuiCol_SliderGrab] = LookColor(t.text[COLOR_YELLOW]);
    c[ImGuiCol_SliderGrabActive] = LookColor(t.text[COLOR_YELLOW]);
    c[ImGuiCol_Button] = LookColor(t.button);
    c[ImGuiCol_ButtonHovered] = mix(t.button, t.selection, 0.6f);
    c[ImGuiCol_ButtonActive] = LookColor(t.selection);
    c[ImGuiCol_Header] = LookColor(t.selection);
    c[ImGuiCol_HeaderHovered] = mix(t.selection, t.button, 0.4f);
    c[ImGuiCol_HeaderActive] = LookColor(t.selection);
    c[ImGuiCol_Separator] = LookColor(t.panel_line);
    c[ImGuiCol_SeparatorHovered] = LookColor(t.panel_border);
    c[ImGuiCol_SeparatorActive] = LookColor(t.panel_border);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableHeaderBg] = mix(t.panel, t.panel_line, 0.45f, 0.95f);
    c[ImGuiCol_TableBorderStrong] = LookColor(t.panel_line);
    c[ImGuiCol_TableBorderLight] = LookColor(t.panel_line, 0.6f);
    c[ImGuiCol_TableRowBg] = LookColor(t.panel, 0.80f);
    c[ImGuiCol_TableRowBgAlt] = mix(t.panel, t.button, 0.5f, 0.80f);
    c[ImGuiCol_PlotHistogram] = LookColor(t.map_standard);
    c[ImGuiCol_PlotHistogramHovered] = LookColor(t.map_selected);
    c[ImGuiCol_TextSelectedBg] = LookColor(t.selection, 0.7f);
    c[ImGuiCol_ModalWindowDimBg] = LookColor(t.background, 0.6f);
}

// Windows 11 draws the title bar in the theme's background
void Look::ApplyTitleBar() const
{
    const Theme &t = g_Themes[m_theme];
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB((t.background >> 16) & 0xFF, (t.background >> 8) & 0xFF, t.background & 0xFF);
    const COLORREF text = RGB((t.text[COLOR_WHITE] >> 16) & 0xFF, (t.text[COLOR_WHITE] >> 8) & 0xFF, t.text[COLOR_WHITE] & 0xFF);
    DwmSetWindowAttribute(m_hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(m_hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
}

void Look::BuildGradient()
{
    const Theme &t = g_Themes[m_theme];
    BuildBackgroundGradient(t.background, t.map_standard, (DWORD *)m_gradient_texels.data());
    m_gradient_theme = m_theme;
    if (m_gradient != nullptr)
    {
        m_gradient->Release();
        m_gradient = nullptr;
    }

    // 0xAARRGGBB in memory is B, G, R, A
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = BACKGROUND_GRADIENT_WIDTH;
    desc.Height = BACKGROUND_GRADIENT_HEIGHT;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = m_gradient_texels.data();
    data.SysMemPitch = BACKGROUND_GRADIENT_WIDTH * sizeof(D3DCOLOR);
    ID3D11Texture2D *texture = nullptr;
    if (SUCCEEDED(m_device->CreateTexture2D(&desc, &data, &texture)))
    {
        m_device->CreateShaderResourceView(texture, nullptr, &m_gradient);
        texture->Release();
    }
}

void Look::UseBackgroundSampler(const ImDrawList *, const ImDrawCmd *command)
{
    const Look *look = (const Look *)command->UserCallbackData;
    look->m_context->PSSetSamplers(0, 1, &look->m_sampler);
}


Look::Look(HWND hwnd, ID3D11Device *device, ID3D11DeviceContext *context)
    : m_hwnd(hwnd), m_device(device), m_context(context), m_gradient_texels(BACKGROUND_GRADIENT_WIDTH * BACKGROUND_GRADIENT_HEIGHT),
      m_xy(BACKGROUND_VERTEX_COUNT * 2), m_uv(BACKGROUND_VERTEX_COUNT * 2)
{
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&desc, &m_sampler);

    m_theme = load_theme();
    ApplyStyle();
    ApplyTitleBar();
}

Look::~Look()
{
    if (m_gradient != nullptr)
    {
        m_gradient->Release();
    }
    if (m_sampler != nullptr)
    {
        m_sampler->Release();
    }
}

int Look::ThemeCount()
{
    return g_ThemeCount;
}

const Theme &Look::ThemeAt(int index)
{
    return g_Themes[index];
}

int Look::ThemeIndex() const
{
    return m_theme;
}

const Theme &Look::CurrentTheme() const
{
    return g_Themes[m_theme];
}

void Look::SetTheme(int index)
{
    if (index < 0 || index >= g_ThemeCount || index == m_theme)
    {
        return;
    }
    m_theme = index;
    ApplyStyle();
    ApplyTitleBar();
    save_theme(m_theme);
}

void Look::DrawBackground()
{
    if (m_gradient_theme != m_theme)
    {
        BuildGradient();
    }
    const float step = ImGui::GetIO().DeltaTime;
    m_seconds += step < MAX_STEP_SECONDS ? step : MAX_STEP_SECONDS;

    const ImVec2 size = ImGui::GetMainViewport()->Size;
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), LookColorU32(g_Themes[m_theme].background));
    if (size.x < 1.0f || size.y < 1.0f)
    {
        return;
    }

    const FLOAT *xy = m_xy.data();
    const FLOAT *uv = m_uv.data();
    const int count = BuildBackgroundMesh(size.x, size.y, m_seconds, m_xy.data(), m_uv.data());
    if (m_gradient != nullptr && m_sampler != nullptr)
    {
        draw->AddCallback(UseBackgroundSampler, this);
        draw->PushTextureID((ImTextureID)(intptr_t)m_gradient);
    }
    draw->PrimReserve(count, count);
    for (int i = 0; i < count; i++)
    {
        ImU32 color = IM_COL32_WHITE;
        ImVec2 texel_uv(uv[i * 2], uv[i * 2 + 1]);
        if (m_gradient == nullptr)
        {
            // Without the texture: the gradient color at the corner (middle
            // dither row), on the font atlas' white pixel
            int texel = (int)(uv[i * 2] * BACKGROUND_GRADIENT_WIDTH);
            texel = texel < 0 ? 0 : texel >= BACKGROUND_GRADIENT_WIDTH ? BACKGROUND_GRADIENT_WIDTH - 1 : texel;
            color = LookColorU32(m_gradient_texels[(BACKGROUND_DITHER_ROWS / 2) * BACKGROUND_GRADIENT_WIDTH + texel]);
            texel_uv = ImGui::GetFontTexUvWhitePixel();
        }
        draw->PrimWriteIdx((ImDrawIdx)draw->_VtxCurrentIdx);
        draw->PrimWriteVtx(ImVec2(origin.x + xy[i * 2], origin.y + xy[i * 2 + 1]), texel_uv, color);
    }
    if (m_gradient != nullptr && m_sampler != nullptr)
    {
        draw->PopTextureID();
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
}

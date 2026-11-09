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

HWND g_hwnd = nullptr;
ID3D11Device *g_device = nullptr;
ID3D11DeviceContext *g_context = nullptr;
ID3D11ShaderResourceView *g_gradient = nullptr;
ID3D11SamplerState *g_sampler = nullptr;
int g_theme = 0;
int g_gradient_theme = -1;
double g_seconds = 0.0;

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

void save_theme()
{
    wchar_t path[MAX_PATH];
    settings_path(path, MAX_PATH);
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"w") != 0 || file == nullptr)
    {
        return; // A read-only folder: not remembered
    }
    fprintf(file, "theme=%ls\n", g_Themes[g_theme].name);
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

void apply_style()
{
    const Theme &t = g_Themes[g_theme];
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
void apply_title_bar()
{
    const Theme &t = g_Themes[g_theme];
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB((t.background >> 16) & 0xFF, (t.background >> 8) & 0xFF, t.background & 0xFF);
    const COLORREF text = RGB((t.text[COLOR_WHITE] >> 16) & 0xFF, (t.text[COLOR_WHITE] >> 8) & 0xFF, t.text[COLOR_WHITE] & 0xFF);
    DwmSetWindowAttribute(g_hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(g_hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
}

D3DCOLOR g_gradient_texels[BACKGROUND_GRADIENT_WIDTH * BACKGROUND_GRADIENT_HEIGHT];

void build_gradient()
{
    const Theme &t = g_Themes[g_theme];
    build_background_gradient(t.background, t.map_standard, (DWORD *)g_gradient_texels);
    g_gradient_theme = g_theme;
    if (g_gradient != nullptr)
    {
        g_gradient->Release();
        g_gradient = nullptr;
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
    data.pSysMem = g_gradient_texels;
    data.SysMemPitch = BACKGROUND_GRADIENT_WIDTH * sizeof(D3DCOLOR);
    ID3D11Texture2D *texture = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture)))
    {
        g_device->CreateShaderResourceView(texture, nullptr, &g_gradient);
        texture->Release();
    }
}

// Inside ImGui's drawing: exact texels, the dither rows repeating down
void use_background_sampler(const ImDrawList *, const ImDrawCmd *)
{
    g_context->PSSetSamplers(0, 1, &g_sampler);
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

void LookInit(HWND hwnd, ID3D11Device *device, ID3D11DeviceContext *context)
{
    g_hwnd = hwnd;
    g_device = device;
    g_context = context;

    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    g_device->CreateSamplerState(&desc, &g_sampler);

    g_theme = load_theme();
    apply_style();
    apply_title_bar();
}

void LookShutdown()
{
    if (g_gradient != nullptr)
    {
        g_gradient->Release();
        g_gradient = nullptr;
    }
    if (g_sampler != nullptr)
    {
        g_sampler->Release();
        g_sampler = nullptr;
    }
}

int LookThemeCount()
{
    return g_ThemeCount;
}

const Theme &LookThemeAt(int index)
{
    return g_Themes[index];
}

int LookThemeIndex()
{
    return g_theme;
}

const Theme &LookTheme()
{
    return g_Themes[g_theme];
}

void LookSetTheme(int index)
{
    if (index < 0 || index >= g_ThemeCount || index == g_theme)
    {
        return;
    }
    g_theme = index;
    apply_style();
    apply_title_bar();
    save_theme();
}

void LookDrawBackground()
{
    static FLOAT xy[BACKGROUND_VERTEX_COUNT * 2];
    static FLOAT uv[BACKGROUND_VERTEX_COUNT * 2];

    if (g_gradient_theme != g_theme)
    {
        build_gradient();
    }
    const float step = ImGui::GetIO().DeltaTime;
    g_seconds += step < MAX_STEP_SECONDS ? step : MAX_STEP_SECONDS;

    const ImVec2 size = ImGui::GetMainViewport()->Size;
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), LookColorU32(g_Themes[g_theme].background));
    if (size.x < 1.0f || size.y < 1.0f)
    {
        return;
    }

    const int count = build_background_mesh(size.x, size.y, g_seconds, xy, uv);
    if (g_gradient != nullptr && g_sampler != nullptr)
    {
        draw->AddCallback(use_background_sampler, nullptr);
        draw->PushTextureID((ImTextureID)(intptr_t)g_gradient);
    }
    draw->PrimReserve(count, count);
    for (int i = 0; i < count; i++)
    {
        ImU32 color = IM_COL32_WHITE;
        ImVec2 texel_uv(uv[i * 2], uv[i * 2 + 1]);
        if (g_gradient == nullptr)
        {
            // Without the texture: the gradient color at the corner (middle
            // dither row), on the font atlas' white pixel
            int texel = (int)(uv[i * 2] * BACKGROUND_GRADIENT_WIDTH);
            texel = texel < 0 ? 0 : texel >= BACKGROUND_GRADIENT_WIDTH ? BACKGROUND_GRADIENT_WIDTH - 1 : texel;
            color = LookColorU32(g_gradient_texels[(BACKGROUND_DITHER_ROWS / 2) * BACKGROUND_GRADIENT_WIDTH + texel]);
            texel_uv = ImGui::GetFontTexUvWhitePixel();
        }
        draw->PrimWriteIdx((ImDrawIdx)draw->_VtxCurrentIdx);
        draw->PrimWriteVtx(ImVec2(origin.x + xy[i * 2], origin.y + xy[i * 2 + 1]), texel_uv, color);
    }
    if (g_gradient != nullptr && g_sampler != nullptr)
    {
        draw->PopTextureID();
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
}

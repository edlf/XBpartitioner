#ifndef FATXGUI_LOOK_H_
#define FATXGUI_LOOK_H_

// The Xbox app's themes and moving background, on the PC

#include <windows.h>
#include <d3d11.h>

#include <vector>

#include "imgui.h"
#include "theme.h"

// The theme in use (remembered in fatxgui.ini), applied to ImGui and the
// title bar, and the background drawn in it
class Look
{
  public:
    // Applies the remembered theme: create it once ImGui's style is set up
    Look(HWND hwnd, ID3D11Device *device, ID3D11DeviceContext *context);
    ~Look();
    Look(const Look &) = delete;
    Look &operator=(const Look &) = delete;

    static int ThemeCount();
    static const Theme &ThemeAt(int index);
    int ThemeIndex() const;
    const Theme &CurrentTheme() const;
    // Applies it to ImGui and the title bar, and remembers it
    void SetTheme(int index);

    // Call first in a frame: fills the window behind everything
    void DrawBackground();

  private:
    void ApplyStyle() const;
    void ApplyTitleBar() const;
    void BuildGradient();
    // Inside ImGui's drawing: exact texels, the dither rows repeating down
    static void UseBackgroundSampler(const ImDrawList *list, const ImDrawCmd *command);

    const HWND m_hwnd;
    ID3D11Device *const m_device;
    ID3D11DeviceContext *const m_context;
    ID3D11ShaderResourceView *m_gradient = nullptr;
    ID3D11SamplerState *m_sampler = nullptr;
    std::vector<D3DCOLOR> m_gradient_texels;
    int m_theme = 0;
    int m_gradient_theme = -1;
    double m_seconds = 0.0; // The background's clock
    std::vector<FLOAT> m_xy; // The background's mesh, made each frame
    std::vector<FLOAT> m_uv;
};

// A theme color, with alpha from the color unless one is given
ImVec4 LookColor(D3DCOLOR color, float alpha = -1.0f);
ImU32 LookColorU32(D3DCOLOR color, float alpha = -1.0f);

#endif // FATXGUI_LOOK_H_

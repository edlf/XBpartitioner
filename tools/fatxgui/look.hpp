#ifndef FATXGUI_LOOK_H_
#define FATXGUI_LOOK_H_

// The Xbox app's themes and moving background, on the PC

#include <windows.h>
#include <d3d11.h>

#include "imgui.h"
#include "theme.h"

void LookInit(HWND hwnd, ID3D11Device *device, ID3D11DeviceContext *context);
void LookShutdown();

int LookThemeCount();
const Theme &LookThemeAt(int index);
int LookThemeIndex();
const Theme &LookTheme();
// Applies it to ImGui and the title bar, and remembers it (fatxgui.ini)
void LookSetTheme(int index);

// A theme color, with alpha from the color unless one is given
ImVec4 LookColor(D3DCOLOR color, float alpha = -1.0f);
ImU32 LookColorU32(D3DCOLOR color, float alpha = -1.0f);

// Call first in a frame: fills the window behind everything
void LookDrawBackground();

#endif // FATXGUI_LOOK_H_

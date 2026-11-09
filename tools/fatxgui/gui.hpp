#ifndef FATXGUI_GUI_H_
#define FATXGUI_GUI_H_

#include <windows.h>
#include <d3d11.h>

// The UI, drawn with Dear ImGui by main.cpp's loop
void GuiInit(HWND hwnd, float dpi_scale, ID3D11Device *device, ID3D11DeviceContext *context);
void GuiFrame();
void GuiShutdown();

// False while a job runs (it shows why instead)
bool GuiCanClose();

// A file dropped on the window: opened as an image
void GuiDropFile(const wchar_t *path);

#endif // FATXGUI_GUI_H_

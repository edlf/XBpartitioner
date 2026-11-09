// fatxgui: a window for fatxtool's work. This file is the Win32 and
// Direct3D 11 shell around Dear ImGui (from its example); gui.cpp is the UI.

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <objbase.h>
#include <shellapi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "gui.hpp"

#pragma comment(lib, "d3d11.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{

ID3D11Device *g_device = nullptr;
ID3D11DeviceContext *g_context = nullptr;
IDXGISwapChain *g_swap_chain = nullptr;
bool g_occluded = false;
UINT g_resize_width = 0;
UINT g_resize_height = 0;
ID3D11RenderTargetView *g_target = nullptr;

void create_target()
{
    ID3D11Texture2D *back_buffer = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer != nullptr)
    {
        g_device->CreateRenderTargetView(back_buffer, nullptr, &g_target);
        back_buffer->Release();
    }
}

void release_target()
{
    if (g_target != nullptr)
    {
        g_target->Release();
        g_target = nullptr;
    }
}

bool create_device(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) // No GPU: the software renderer
    {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
    }
    if (hr != S_OK)
    {
        return false;
    }
    create_target();
    return true;
}

void destroy_device()
{
    release_target();
    if (g_swap_chain != nullptr)
    {
        g_swap_chain->Release();
        g_swap_chain = nullptr;
    }
    if (g_context != nullptr)
    {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device != nullptr)
    {
        g_device->Release();
        g_device = nullptr;
    }
}

LRESULT WINAPI window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
    {
        return true;
    }

    switch (msg)
    {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED)
        {
            return 0;
        }
        g_resize_width = (UINT)LOWORD(lparam);
        g_resize_height = (UINT)HIWORD(lparam);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        info->ptMinTrackSize.x = 720;
        info->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) // No ALT application menu
        {
            return 0;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wparam;
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) != 0)
        {
            GuiDropFile(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_CLOSE:
        // Not while something is being written
        if (!GuiCanClose())
        {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); // File dialogs

    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"fatxgui";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES,
                                wc.lpszClassName,
                                L"XBpartitioner FATX tools",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                (int)(1100 * scale),
                                (int)(760 * scale),
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);

    if (!create_device(hwnd))
    {
        destroy_device();
        UnregisterClassW(wc.lpszClassName, instance);
        MessageBoxW(nullptr, L"Direct3D 11 isn't available.", L"XBpartitioner FATX tools", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Nothing saved next to the exe

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    GuiInit(hwnd, scale, g_device, g_context);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
            {
                done = true;
            }
        }
        if (done)
        {
            break;
        }

        // Minimized or covered: don't spin
        if (g_occluded && g_swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            Sleep(10);
            continue;
        }
        g_occluded = false;

        if (g_resize_width != 0 && g_resize_height != 0)
        {
            release_target();
            g_swap_chain->ResizeBuffers(0, g_resize_width, g_resize_height, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_width = g_resize_height = 0;
            create_target();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        GuiFrame();
        ImGui::Render();

        const float clear[4] = {0.08f, 0.09f, 0.10f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_target, nullptr);
        g_context->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const HRESULT hr = g_swap_chain->Present(1, 0); // With vsync
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    GuiShutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    destroy_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    CoUninitialize();
    return 0;
}

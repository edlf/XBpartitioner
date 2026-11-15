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

// The window's Direct3D 11 device and swap chain, which ImGui draws into
class Direct3D
{
  public:
    Direct3D() = default;
    ~Direct3D()
    {
        ReleaseTarget();
        Release(m_swap_chain);
        Release(m_context);
        Release(m_device);
    }
    Direct3D(const Direct3D &) = delete;
    Direct3D &operator=(const Direct3D &) = delete;

    // False if there's no Direct3D 11, not even the software renderer
    bool Create(HWND hwnd)
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

        HRESULT hr = CreateWith(D3D_DRIVER_TYPE_HARDWARE, sd);
        if (hr == DXGI_ERROR_UNSUPPORTED) // No GPU: the software renderer
        {
            hr = CreateWith(D3D_DRIVER_TYPE_WARP, sd);
        }
        if (hr != S_OK)
        {
            return false;
        }
        CreateTarget();
        return true;
    }

    ID3D11Device *Device() const
    {
        return m_device;
    }
    ID3D11DeviceContext *Context() const
    {
        return m_context;
    }

    // From WM_SIZE: applied before the next frame
    void RequestResize(UINT width, UINT height)
    {
        m_resize_width = width;
        m_resize_height = height;
    }

    // Minimized or covered since the last frame, and still so
    bool StillOccluded()
    {
        if (m_occluded && m_swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            return true;
        }
        m_occluded = false;
        return false;
    }

    void ApplyPendingResize()
    {
        if (m_resize_width != 0 && m_resize_height != 0)
        {
            ReleaseTarget();
            m_swap_chain->ResizeBuffers(0, m_resize_width, m_resize_height, DXGI_FORMAT_UNKNOWN, 0);
            m_resize_width = m_resize_height = 0;
            CreateTarget();
        }
    }

    // Before ImGui draws the frame
    void Clear()
    {
        const float clear[4] = {0.08f, 0.09f, 0.10f, 1.0f};
        m_context->OMSetRenderTargets(1, &m_target, nullptr);
        m_context->ClearRenderTargetView(m_target, clear);
    }

    // With vsync
    void Present()
    {
        const HRESULT hr = m_swap_chain->Present(1, 0);
        m_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

  private:
    HRESULT CreateWith(D3D_DRIVER_TYPE driver, const DXGI_SWAP_CHAIN_DESC &sd)
    {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL level;
        return D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &m_swap_chain, &m_device, &level, &m_context);
    }

    void CreateTarget()
    {
        ID3D11Texture2D *back_buffer = nullptr;
        m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (back_buffer != nullptr)
        {
            m_device->CreateRenderTargetView(back_buffer, nullptr, &m_target);
            back_buffer->Release();
        }
    }

    void ReleaseTarget()
    {
        Release(m_target);
    }

    template <typename T> static void Release(T *&object)
    {
        if (object != nullptr)
        {
            object->Release();
            object = nullptr;
        }
    }

    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_context = nullptr;
    IDXGISwapChain *m_swap_chain = nullptr;
    ID3D11RenderTargetView *m_target = nullptr;
    bool m_occluded = false;
    UINT m_resize_width = 0;
    UINT m_resize_height = 0;
};

// What window_proc reaches through the window's user data
struct WindowState
{
    Direct3D *direct3d;
    Gui *gui; // Null until the UI exists
};

LRESULT WINAPI window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
    {
        return true;
    }

    WindowState *state = (WindowState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    Gui *gui = state != nullptr ? state->gui : nullptr;
    switch (msg)
    {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED || state == nullptr)
        {
            return 0;
        }
        state->direct3d->RequestResize((UINT)LOWORD(lparam), (UINT)HIWORD(lparam));
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
        if (gui != nullptr && DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) != 0)
        {
            gui->DropFile(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_CLOSE:
        // Not while something is being written
        if (gui != nullptr && !gui->CanClose())
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

const wchar_t WINDOW_CLASS[] = L"fatxgui";
const wchar_t WINDOW_TITLE[] = L"XBpartitioner FATX tools";

// Sized for the primary monitor's DPI; files can be dropped on it
HWND create_main_window(HINSTANCE instance, float scale)
{
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&wc);
    return CreateWindowExW(WS_EX_ACCEPTFILES,
                           WINDOW_CLASS,
                           WINDOW_TITLE,
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           (int)(1100 * scale),
                           (int)(760 * scale),
                           nullptr,
                           nullptr,
                           instance,
                           nullptr);
}

void init_imgui(HWND hwnd, const Direct3D &direct3d)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Nothing saved next to the exe

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(direct3d.Device(), direct3d.Context());
}

void shutdown_imgui()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

// Handles every waiting message; false once the window has closed
bool pump_messages()
{
    bool open = true;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            open = false;
        }
    }
    return open;
}

void render_frame(Gui &gui, Direct3D &direct3d)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    gui.Frame();
    ImGui::Render();

    direct3d.Clear();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    direct3d.Present();
}

void run_message_loop(Gui &gui, Direct3D &direct3d)
{
    while (pump_messages())
    {
        if (direct3d.StillOccluded())
        {
            Sleep(10); // Don't spin
            continue;
        }
        direct3d.ApplyPendingResize();
        render_frame(gui, direct3d);
    }
}

// The UI, for as long as the window is open
void run_gui(HWND hwnd, float scale, WindowState &state)
{
    Gui gui(GuiWindow{hwnd, scale, state.direct3d->Device(), state.direct3d->Context()});
    state.gui = &gui;
    run_message_loop(gui, *state.direct3d);
    state.gui = nullptr;
}

// Direct3D, ImGui and the UI, until the window closes. False if there's no
// Direct3D 11.
bool run_window(HWND hwnd, float scale)
{
    Direct3D direct3d;
    if (!direct3d.Create(hwnd))
    {
        return false;
    }
    WindowState state = {&direct3d, nullptr};
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&state);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    init_imgui(hwnd, direct3d);
    run_gui(hwnd, scale, state);
    shutdown_imgui();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); // File dialogs

    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = create_main_window(instance, scale);
    if (!run_window(hwnd, scale))
    {
        UnregisterClassW(WINDOW_CLASS, instance);
        MessageBoxW(nullptr, L"Direct3D 11 isn't available.", WINDOW_TITLE, MB_ICONERROR);
        return 1;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(WINDOW_CLASS, instance);
    CoUninitialize();
    return 0;
}

#ifndef FATXGUI_GUI_H_
#define FATXGUI_GUI_H_

#include <windows.h>
#include <d3d11.h>
#include <memory>

// The window the UI draws in, and its Direct3D device
struct GuiWindow
{
    HWND hwnd;
    float dpi_scale;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
};

// The UI, drawn with Dear ImGui by main.cpp's loop. Create it once ImGui is
// set up and destroy it before ImGui shuts down; it waits for a running job.
class Gui
{
  public:
    explicit Gui(const GuiWindow &window);
    ~Gui();
    Gui(const Gui &) = delete;
    Gui &operator=(const Gui &) = delete;

    void Frame();

    // False while a job writes (it shows why instead)
    bool CanClose();

    // A file dropped on the window: opened as an image
    void DropFile(const wchar_t *path);

  private:
    struct Parts; // gui.cpp
    std::unique_ptr<Parts> m_parts;
};

#endif // FATXGUI_GUI_H_

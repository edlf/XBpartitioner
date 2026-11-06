#ifndef UI_H_
#define UI_H_

#include <xtl.h>
#include <xfont.h>

// All drawing goes through Renderer, in a 640x480 layout space. The renderer
// maps layout coordinates to the real back buffer: 1:1 for 480i/480p, scaled
// 1.5x and centered (960x720 with side bars) for 720p.
#define LAYOUT_WIDTH  640
#define LAYOUT_HEIGHT 480

enum VideoMode
{
    VIDEO_MODE_480I = 0,
    VIDEO_MODE_480P = 1,
    VIDEO_MODE_720P = 2
};

// Order must match g_ColorStyles in ui.cpp
enum Color
{
    COLOR_WHITE = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_WHITE_BG,
    COLOR_RED_BG,
    COLOR_GREY,
    COLOR_DARK_YELLOW,
    COLOR_LIGHT_GREY, // Body text on warning screens
    COLOR_WARNING,    // Warning screen title
    COLOR_COUNT
};

enum Font
{
    FONT_TEXT = 0, // Proportional, everything except the table
    FONT_TABLE,    // Monospace, partition table values
    FONT_TITLE,    // Large bold, warning titles
    FONT_COUNT
};

class Renderer
{
public:
    Renderer();
    ~Renderer();

    // Picks the video mode from the dashboard settings, creates the device and loads the fonts
    void Init();
    // Releases everything. Safe to call more than once.
    void Shutdown();

    VideoMode GetVideoMode() const { return m_VideoMode; }
    LPCWSTR GetVideoModeName() const;

    // Clears the back buffer and sets up the render states for the frame
    void BeginFrame();
    // Shows the back buffer
    void EndFrame();

    // Text, in layout coordinates. align is XFONT_LEFT, XFONT_CENTER or XFONT_RIGHT.
    void Text(Font font, LPCWSTR text, int x, int y, Color color, int align = XFONT_LEFT);
    // Rectangle with a vertical gradient, in layout coordinates
    void Rect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color);
    // Fills the whole back buffer, side bars included. Used with an alpha for fades.
    void FillScreen(D3DCOLOR color);
    // Fades whatever is on screen to black
    void FadeOut();

private:
    bool CreateDevice(VideoMode mode);

    FLOAT ToScreenX(FLOAT x) const { return m_OffsetX + x * m_Scale; }
    FLOAT ToScreenY(FLOAT y) const { return m_OffsetY + y * m_Scale; }
    // Rectangle in back buffer coordinates
    void ScreenRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color);

    LPDIRECT3D8 m_pD3D;
    LPDIRECT3DDEVICE8 m_pDevice;
    XFONT *m_pFonts[FONT_COUNT];

    VideoMode m_VideoMode;
    unsigned int m_Width;
    unsigned int m_Height;
    FLOAT m_Scale;
    FLOAT m_OffsetX;
    FLOAT m_OffsetY;
};

#endif // UI_H_

#ifndef UI_H_
#define UI_H_

#include <xtl.h>
#include <xfont.h>

// All drawing goes through Renderer, in screen pixels of a 4:3 area (640x480,
// or 960x720 for 720p). The renderer centers that area on the screen (720p:
// 160 pixel side bars) and draws 1:1. Positions come from a Layout per video
// mode (xbpartitioner.h), so nothing is scaled or rounded.

enum VideoMode
{
    VIDEO_MODE_480I = 0,
    VIDEO_MODE_480P = 1,
    VIDEO_MODE_720P = 2
};

// Text colors, by role: each theme (g_Themes in ui.cpp) gives one per role,
// so the names are what they look like in the default theme.
// Order must match Theme::text in the themes and g_ColorOpaque in ui.cpp.
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

// Every color the UI draws with. Text uses one of the Color roles, the rest
// (panels, the partition table, the disk map) the named colors.
struct Theme
{
    LPCWSTR name;
    D3DCOLOR background;        // Behind everything
    D3DCOLOR text[COLOR_COUNT]; // Indexed by Color
    D3DCOLOR text_background;   // Box behind the *_BG text colors
    D3DCOLOR panel;             // Menus, dialogs and the partition table (the alpha lets what's behind show through)
    D3DCOLOR panel_border;
    D3DCOLOR panel_line;        // Under titles and the table headers
    D3DCOLOR selection;         // Selection bars, the selected button or key
    D3DCOLOR button;            // Dialog buttons and keypad keys
    D3DCOLOR map_free;          // Disk map: free space,
    D3DCOLOR map_standard;      // standard partitions (1-5),
    D3DCOLOR map_extended;      // extended partitions (6-14)
    D3DCOLOR map_selected;      // and the selected one
};

enum Font
{
    FONT_TEXT = 0, // Proportional, everything except the table
    FONT_TABLE,    // Monospace, partition table values
    FONT_TITLE,    // Large bold, warning titles
    FONT_COUNT
};

// Characters the GPU text has glyphs for (printable ASCII); others draw as '?'
static const int FONT_FIRST_CHAR = 32;
static const int FONT_LAST_CHAR = 126;
static const int FONT_GLYPH_COUNT = FONT_LAST_CHAR - FONT_FIRST_CHAR + 1;

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
    void EndFrame();

    // Themes: the colors everything is drawn with. SetTheme() takes effect
    // from the next thing drawn; out of range indexes are ignored.
    static int GetThemeCount();
    static const Theme &GetThemeAt(int index);
    // Index of the theme named name (case insensitive), -1 if there is none
    static int FindTheme(const char *name);
    void SetTheme(int index);
    int GetThemeIndex() const { return m_ThemeIndex; }
    const Theme &GetTheme() const { return GetThemeAt(m_ThemeIndex); }

    // Text, in layout pixels. align is XFONT_LEFT, XFONT_CENTER or XFONT_RIGHT.
    // Drawn by the GPU from a texture of the font's characters (see
    // BuildFontAtlas), the same layout XFONT would give; with XFONT itself
    // if that texture couldn't be made.
    void Text(Font font, LPCWSTR text, int x, int y, Color color, int align = XFONT_LEFT);
    // Width of a string in pixels, as Text() lays it out
    int TextWidth(Font font, LPCWSTR text) const;
    // Height of a font's character cell, in pixels (what XFONT_TOP aligns to).
    // Boxes around text are sized from this: text height + padding.
    int FontHeight(Font font) const;
    // Part of the cell below the baseline (tails of p, g, y)
    int FontDescent(Font font) const;
    // Height of the capital letters, from the baseline up
    int FontCapHeight(Font font) const;
    // Top y for text so it looks vertically centered between top and bottom:
    // the capital letters are centered, descenders hang below them and the
    // room for accents above capitals is left out. Centering the whole cell
    // makes text sit too high; centering everything above the baseline still
    // leaves the accent room at the top.
    int TextYCentered(Font font, int top, int bottom) const
    {
        const int cap_height = FontCapHeight(font);
        const int above_caps = FontHeight(font) - FontDescent(font) - cap_height; // Accent room in the cell
        return top + (bottom - top - cap_height) / 2 - above_caps;
    }
    // Height for a box around one line of text: the font plus padding above and
    // below, plus one pixel when needed so the space above and below the
    // capitals is equal (an even leftover). Use with TextYCentered().
    int TextBoxHeight(Font font, int padding) const
    {
        int height = FontHeight(font) + 2 * padding;
        if ((height - FontCapHeight(font)) % 2 != 0)
        {
            height++;
        }
        return height;
    }
    // Rectangle with a vertical gradient, in layout pixels
    void Rect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color);
    // 1 pixel outline just inside the rectangle (x2/y2 exclusive, like Rect)
    void Outline(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR color);
    // Fills the whole back buffer, side bars included. Used with an alpha for fades.
    void FillScreen(D3DCOLOR color);
    // Moving background over the whole back buffer (side bars included), see
    // background.h. Drawn by the GPU; call it first in a frame. It has its own
    // clock: a long gap between two draws (a slow operation) counts as a short
    // step, so it carries on instead of jumping.
    void DrawBackground();


private:
    bool CreateDevice(VideoMode mode);

    // GPU text: each font's characters drawn once with XFONT into a texture
    // (white, the coverage in the alpha), each in a slot as wide as its
    // advance plus pad on both sides for parts that reach past it. Text is
    // then textured quads tinted with the text color.
    struct FontAtlas
    {
        LPDIRECT3DTEXTURE8 texture; // NULL: this font is drawn with XFONT
        int width;                  // Texture size, powers of two
        int height;
        int pad;
        short u[FONT_GLYPH_COUNT];  // Left and top of each character's slot
        short v[FONT_GLYPH_COUNT];
        short advance[FONT_GLYPH_COUNT];
    };
    bool BuildFontAtlas(int font);
    // XFONT on the back buffer, for a font without a texture
    void TextXFont(Font font, LPCWSTR text, long screen_x, long screen_y, Color color, int align);
    // Text is collected and drawn in one go per font: when the font changes,
    // before anything else is drawn (so it stays in order), and at the end of
    // the frame
    void FlushText();

    FLOAT ToScreenX(FLOAT x) const { return m_OffsetX + x; }
    FLOAT ToScreenY(FLOAT y) const { return m_OffsetY + y; }
    // Rectangle in back buffer coordinates
    void ScreenRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color);

    LPDIRECT3D8 m_pD3D;
    LPDIRECT3DDEVICE8 m_pDevice;
    XFONT *m_pFonts[FONT_COUNT];
    FontAtlas m_Atlases[FONT_COUNT];
    int m_TextBatchFont;  // Font of the text waiting in the batch (see FlushText)
    int m_TextBatchCount; // Vertices waiting
    int m_FontHeights[FONT_COUNT];  // Cell heights, read once when the fonts are loaded
    int m_FontDescents[FONT_COUNT]; // Below the baseline, part of the cell height
    int m_FontCapHeights[FONT_COUNT]; // Capital letters above the baseline

    int m_ThemeIndex;
    // The background's gradient (see background.h), for the theme it was made for
    LPDIRECT3DTEXTURE8 m_pBackgroundTexture;
    int m_BackgroundTextureTheme;
    double m_BackgroundSeconds; // Background clock, see DrawBackground()
    DWORD m_BackgroundTick;     // When it was last drawn, 0 before the first time
    VideoMode m_VideoMode;
    unsigned int m_Width;
    unsigned int m_Height;
    FLOAT m_OffsetX;
    FLOAT m_OffsetY;
};

#endif // UI_H_

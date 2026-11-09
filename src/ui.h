#ifndef UI_H_
#define UI_H_

#include <xtl.h>
#include <xfont.h>

#include "theme.h"

// Drawing is in pixels of a 4:3 area (640x480, or 960x720 for 720p),
// centered on the screen and drawn 1:1; positions come from a Layout per mode.

enum VideoMode
{
    VIDEO_MODE_480I = 0,
    VIDEO_MODE_480P = 1,
    VIDEO_MODE_720P = 2
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

    void Init();
    // Safe to call more than once
    void Shutdown();

    VideoMode GetVideoMode() const
    {
        return m_VideoMode;
    }
    LPCWSTR GetVideoModeName() const;

    void BeginFrame();
    void EndFrame();

    // SetTheme() applies from the next draw; out of range indexes are ignored
    static int GetThemeCount();
    static const Theme &GetThemeAt(int index);
    // Case insensitive, -1 if there is none
    static int FindTheme(const char *name);
    void SetTheme(int index);
    int GetThemeIndex() const
    {
        return m_ThemeIndex;
    }
    const Theme &GetTheme() const
    {
        return GetThemeAt(m_ThemeIndex);
    }

    // In layout pixels. Drawn by the GPU from a font texture (BuildFontAtlas),
    // laid out like XFONT; with XFONT itself if there is no texture.
    void Text(Font font, LPCWSTR text, int x, int y, Color color, int align = XFONT_LEFT);
    int TextWidth(Font font, LPCWSTR text) const;
    // The character cell (what XFONT_TOP aligns to)
    int FontHeight(Font font) const;
    // Part of the cell below the baseline (tails of p, g, y)
    int FontDescent(Font font) const;
    int FontCapHeight(Font font) const;
    // Top y so text looks centered: the capitals are centered, descenders hang
    // below and the accent room above is left out
    int TextYCentered(Font font, int top, int bottom) const
    {
        const int cap_height = FontCapHeight(font);
        const int above_caps = FontHeight(font) - FontDescent(font) - cap_height; // Accent room in the cell
        return top + (bottom - top - cap_height) / 2 - above_caps;
    }
    // Font height plus padding above and below, plus a pixel if needed so the
    // space around the capitals is even. Use with TextYCentered().
    int TextBoxHeight(Font font, int padding) const
    {
        int height = FontHeight(font) + 2 * padding;
        if ((height - FontCapHeight(font)) % 2 != 0)
        {
            height++;
        }
        return height;
    }
    // Vertical gradient, in layout pixels
    void Rect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color);
    // 1 pixel outline just inside the rectangle (x2/y2 exclusive, like Rect)
    void Outline(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR color);
    // Fills the whole back buffer, side bars included. Used with an alpha for fades.
    void FillScreen(D3DCOLOR color);
    // Over the whole back buffer; call it first in a frame. Its own clock: a
    // long gap counts as a short step.
    void DrawBackground();

  private:
    bool CreateDevice(VideoMode mode);

    // Each font's characters drawn once with XFONT into a texture (white,
    // coverage in alpha), slots of advance plus pad; text is tinted quads
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
    // Per font batches, flushed when the font changes, before other drawing,
    // and at the end of the frame
    void FlushText();

    FLOAT ToScreenX(FLOAT x) const
    {
        return m_OffsetX + x;
    }
    FLOAT ToScreenY(FLOAT y) const
    {
        return m_OffsetY + y;
    }
    // Rectangle in back buffer coordinates
    void ScreenRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color);

    LPDIRECT3D8 m_pD3D;
    LPDIRECT3DDEVICE8 m_pDevice;
    XFONT *m_pFonts[FONT_COUNT];
    FontAtlas m_Atlases[FONT_COUNT];
    int m_TextBatchFont;  // Font of the text waiting in the batch (see FlushText)
    int m_TextBatchCount;
    int m_FontHeights[FONT_COUNT];  // Read once when the fonts are loaded
    int m_FontDescents[FONT_COUNT]; // Below the baseline, part of the cell height
    int m_FontCapHeights[FONT_COUNT];

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

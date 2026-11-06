#include "stdafx.h"
#include "ui.h"
#include "themes.h"
#include "background.h"

#include <stdlib.h>  // malloc, free for building the font textures
#include <string.h>  // memset
#include <xgraphics.h> // XGSwizzleRect

// Fonts, generated from fonts\ by tools\makefonts.bat.
// makefont declares the arrays as "_declspec(selectany) const BYTE", which is
// invalid C++ (error C2496: selectany needs external linkage, and const data
// at file scope has internal linkage). Defining _declspec away while including
// them turns them into plain const arrays, which is all that is needed since
// they are only included here. The *_720 fonts are 1.5x for 720p.
#define _declspec(x)
#include "font_text.h"
#include "font_title.h"
#include "font_table.h"
#include "font_text_720.h"
#include "font_title_720.h"
#include "font_table_720.h"
#undef _declspec

struct ScreenVertex
{
    D3DXVECTOR4 pos; // Pre-transformed screen position
    DWORD color;
};

#define D3DFVF_SCREENVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

struct TextVertex
{
    D3DXVECTOR4 pos;
    DWORD color;
    FLOAT u, v;
};

#define D3DFVF_TEXTVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// Longest string drawn in one call; longer ones are cut (the UI has nothing near it)
static const int TEXT_MAX_CHARS = 200;
// Text waiting to be drawn (see Renderer::FlushText), up to this many vertices
static const int TEXT_BATCH_VERTICES = 6 * 2048;
static TextVertex g_TextBatch[TEXT_BATCH_VERTICES];

static int glyph_index(wchar_t c)
{
    return (c >= FONT_FIRST_CHAR && c <= FONT_LAST_CHAR) ? c - FONT_FIRST_CHAR : '?' - FONT_FIRST_CHAR;
}

// Text colors drawn on a box (Theme::text_background), indexed by the Color enum
static const bool g_ColorOpaque[COLOR_COUNT] = {
    false, // COLOR_WHITE
    false, // COLOR_RED
    false, // COLOR_GREEN
    false, // COLOR_BLUE
    false, // COLOR_YELLOW
    true,  // COLOR_WHITE_BG
    true,  // COLOR_RED_BG
    false, // COLOR_GREY
    false, // COLOR_DARK_YELLOW
    false, // COLOR_LIGHT_GREY
    false, // COLOR_WARNING
};

int Renderer::GetThemeCount()
{
    return g_ThemeCount;
}

const Theme &Renderer::GetThemeAt(int index)
{
    return g_Themes[(index >= 0 && index < g_ThemeCount) ? index : 0];
}

// Theme names are ASCII
static wchar_t ascii_lower(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
}

int Renderer::FindTheme(const char *name)
{
    for (int i = 0; i < g_ThemeCount; i++)
    {
        const wchar_t *theme_name = g_Themes[i].name;
        int c = 0;
        while (theme_name[c] != L'\0' && name[c] != '\0' &&
               ascii_lower(theme_name[c]) == ascii_lower((wchar_t)(unsigned char)name[c]))
        {
            c++;
        }
        if (theme_name[c] == L'\0' && name[c] == '\0')
        {
            return i;
        }
    }
    return -1;
}

void Renderer::SetTheme(int index)
{
    if (index >= 0 && index < g_ThemeCount)
    {
        m_ThemeIndex = index;
    }
}

struct FontData
{
    const BYTE *data;
    unsigned int size;
    int height; // Pixel height the font was generated at (makefont -h, tools\makefonts.bat)
};

// Indexed by the Font enum
static const FontData g_Fonts480[FONT_COUNT] = {
    {font_text, sizeof(font_text), 18},
    {font_table, sizeof(font_table), 18},
    {font_title, sizeof(font_title), 32},
};

static const FontData g_Fonts720[FONT_COUNT] = {
    {font_text_720, sizeof(font_text_720), 27},
    {font_table_720, sizeof(font_table_720), 27},
    {font_title_720, sizeof(font_title_720), 48},
};

Renderer::Renderer()
    : m_pD3D(NULL), m_pDevice(NULL), m_TextBatchFont(-1), m_TextBatchCount(0), m_ThemeIndex(0),
      m_pBackgroundTexture(NULL), m_BackgroundTextureTheme(-1),
      m_BackgroundSeconds(0.0), m_BackgroundTick(0), m_VideoMode(VIDEO_MODE_480I),
      m_Width(640), m_Height(480), m_OffsetX(0.0f), m_OffsetY(0.0f)
{
    for (int i = 0; i < FONT_COUNT; i++)
    {
        m_pFonts[i] = NULL;
        m_FontHeights[i] = 0;
        m_FontDescents[i] = 0;
        m_FontCapHeights[i] = 0;
        memset(&m_Atlases[i], 0, sizeof(m_Atlases[i]));
    }
}

Renderer::~Renderer()
{
    Shutdown();
}

// Picks the video mode from the AV pack and the dashboard video settings:
//   720p - component (HDTV) pack with 720p enabled
//   480p - component or VGA pack with 480p enabled
//   480i - everything else (composite, s-video, scart, rf)
static VideoMode SelectVideoMode()
{
    const DWORD video_flags = XGetVideoFlags();
    const DWORD av_pack = XGetAVPack();

    if (av_pack == XC_AV_PACK_HDTV && (video_flags & XC_VIDEO_FLAGS_HDTV_720p))
    {
        return VIDEO_MODE_720P;
    }

    if ((av_pack == XC_AV_PACK_HDTV || av_pack == XC_AV_PACK_VGA) && (video_flags & XC_VIDEO_FLAGS_HDTV_480p))
    {
        return VIDEO_MODE_480P;
    }

    return VIDEO_MODE_480I;
}

bool Renderer::CreateDevice(VideoMode mode)
{
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));

    switch (mode)
    {
    case VIDEO_MODE_720P:
        m_Width = 1280;
        m_Height = 720;
        d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
        break;
    case VIDEO_MODE_480P:
        m_Width = 640;
        m_Height = 480;
        d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE;
        break;
    default:
        m_Width = 640;
        m_Height = 480;
        break;
    }

    d3dpp.BackBufferWidth = m_Width;
    d3dpp.BackBufferHeight = m_Height;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    // Anti-aliasing, so the background's triangle edges don't crawl as they
    // move: 4 samples per pixel at 480, 2 at 720p (memory). The LINEAR modes
    // only average a pixel's own samples, so text and boxes, which sit
    // exactly on pixels, stay as sharp as without. Without it if refused.
    d3dpp.MultiSampleType = (mode == VIDEO_MODE_720P) ? D3DMULTISAMPLE_2_SAMPLES_MULTISAMPLE_LINEAR
                                                      : D3DMULTISAMPLE_4_SAMPLES_MULTISAMPLE_LINEAR;

    m_pDevice = NULL;
    if (FAILED(m_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &m_pDevice)) || m_pDevice == NULL)
    {
        m_pDevice = NULL;
        d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
        if (FAILED(m_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &m_pDevice)) || m_pDevice == NULL)
        {
            m_pDevice = NULL;
            return false;
        }
    }

    m_VideoMode = mode;

    // The layout is a 4:3 area the height of the screen, centered horizontally
    // (720p: 960 wide, 160 pixel bars). Whole pixels, so text lands on pixels.
    const unsigned int layout_width = m_Height * 4 / 3;
    m_OffsetX = (FLOAT)((m_Width - layout_width) / 2);
    m_OffsetY = 0.0f;

    return true;
}

void Renderer::Init()
{
    m_pD3D = Direct3DCreate8(D3D_SDK_VERSION);

    // If the selected mode is refused, fall back to plain 480i which every setup supports
    const VideoMode mode = SelectVideoMode();
    if (!CreateDevice(mode) && mode != VIDEO_MODE_480I)
    {
        CreateDevice(VIDEO_MODE_480I);
    }

    // Nothing can be shown without a device, go back to the dashboard
    if (m_pDevice == NULL)
    {
        Shutdown();
        LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};
        XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
        return;
    }

    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);

    // Fonts are pre-rendered for each mode, so text stays sharp at 720p
    const FontData *fonts = (m_VideoMode == VIDEO_MODE_720P) ? g_Fonts720 : g_Fonts480;
    for (int i = 0; i < FONT_COUNT; i++)
    {
        XFONT_OpenBitmapFontFromMemory(fonts[i].data, fonts[i].size, &m_pFonts[i]);

        // Cell height and descent of the font as drawn in this mode, so boxes
        // can be sized around the text and text centered in them. Falls back to
        // the height the font was generated at, and DejaVu's descent (about
        // 0.24 of the height), if XFONT reports nothing (or something
        // implausible) for it.
        m_FontHeights[i] = fonts[i].height;
        m_FontDescents[i] = (fonts[i].height * 24 + 50) / 100;
        // XFONT has no cap height; DejaVu's capitals are 0.73 of the font size
        // (13px at 18, 20px at 27, 23px at 32). Used to center text on its capitals.
        m_FontCapHeights[i] = (fonts[i].height * 73 + 50) / 100;
        if (m_pFonts[i] != NULL)
        {
            unsigned int cell_height = 0;
            unsigned int descent = 0;
            m_pFonts[i]->GetFontMetrics(&cell_height, &descent);
            if (cell_height >= (unsigned int)fonts[i].height && cell_height <= (unsigned int)fonts[i].height * 2)
            {
                m_FontHeights[i] = (int)cell_height;
                if (descent > 0 && descent < cell_height / 2)
                {
                    m_FontDescents[i] = (int)descent;
                }
            }
        }

        BuildFontAtlas(i);
    }
}

bool Renderer::BuildFontAtlas(int font)
{
    FontAtlas &atlas = m_Atlases[font];
    XFONT *pFont = m_pFonts[font];
    const int cell = m_FontHeights[font];
    if (pFont == NULL || cell <= 0)
    {
        return false;
    }

    // XFONT lays text out by each character's advance (these bitmap fonts
    // have no kerning), so the advances are all that's needed to match it.
    // Measured as the extent of two of it less one: right even if the extent
    // includes the part of the last glyph that reaches past its advance.
    for (int g = 0; g < FONT_GLYPH_COUNT; g++)
    {
        const wchar_t pair[2] = {(wchar_t)(FONT_FIRST_CHAR + g), (wchar_t)(FONT_FIRST_CHAR + g)};
        unsigned one = 0;
        unsigned two = 0;
        if (FAILED(pFont->GetTextExtent(pair, 1, &one)) || FAILED(pFont->GetTextExtent(pair, 2, &two)) ||
            two < one || two - one > 255)
        {
            return false;
        }
        atlas.advance[g] = (short)(two - one);
    }
    atlas.pad = cell / 3 + 1;

    // Slots in rows, two pixels apart; the smallest texture they fit in
    int width = 128;
    int height = 0;
    while (true)
    {
        int x = 0;
        int y = 0;
        bool fits = true;
        for (int g = 0; g < FONT_GLYPH_COUNT && fits; g++)
        {
            const int slot = atlas.advance[g] + 2 * atlas.pad;
            if (slot > width)
            {
                fits = false;
                break;
            }
            if (x + slot > width)
            {
                x = 0;
                y += cell + 2;
            }
            atlas.u[g] = (short)x;
            atlas.v[g] = (short)y;
            x += slot + 2;
        }
        height = 1;
        while (height < y + cell)
        {
            height *= 2;
        }
        if (fits && height <= width)
        {
            break;
        }
        width *= 2;
        if (width > 2048)
        {
            return false;
        }
    }
    atlas.width = width;
    atlas.height = height;

    // Drawn white on black with XFONT, so each pixel's brightness is the
    // character's coverage there (its smoothing included)
    DWORD *pixels = (DWORD *)malloc(width * height * sizeof(DWORD));
    if (pixels == NULL)
    {
        return false;
    }
    memset(pixels, 0, width * height * sizeof(DWORD));

    // Each character is drawn on its own, in a scratch area with a cell of
    // room all around it, and only its slot is copied into the texture: a
    // character that draws past its slot can't end up in another's (that put
    // stray marks next to some letters).
    const int scratch_width = 2 * cell + atlas.pad * 2 + 256;
    const int scratch_height = 3 * cell;
    DWORD *scratch = (DWORD *)malloc(scratch_width * scratch_height * sizeof(DWORD));
    if (scratch == NULL)
    {
        free(pixels);
        return false;
    }

    pFont->SetTextColor(D3DCOLOR_XRGB(255, 255, 255));
    pFont->SetBkMode(XFONT_TRANSPARENT);
    pFont->SetTextAlignment(XFONT_TOP | XFONT_LEFT);
    for (int g = 0; g < FONT_GLYPH_COUNT; g++)
    {
        const wchar_t ch = (wchar_t)(FONT_FIRST_CHAR + g);
        const int slot = atlas.advance[g] + 2 * atlas.pad;
        memset(scratch, 0, scratch_width * scratch_height * sizeof(DWORD));
        // The slot starts at (cell, cell) in the scratch area
        pFont->TextOutToMemory(scratch, scratch_width * sizeof(DWORD), scratch_width, scratch_height, D3DFMT_LIN_X8R8G8B8,
                               &ch, 1, cell + atlas.pad, cell);
        for (int row = 0; row < cell; row++)
        {
            memcpy(&pixels[(atlas.v[g] + row) * width + atlas.u[g]], &scratch[(cell + row) * scratch_width + cell],
                   slot * sizeof(DWORD));
        }
    }
    free(scratch);

    // White, with the coverage as the alpha
    bool any = false;
    for (int i = 0; i < width * height; i++)
    {
        const DWORD p = pixels[i];
        DWORD a = (p >> 16) & 0xFF;
        const DWORD g = (p >> 8) & 0xFF;
        const DWORD b = p & 0xFF;
        if (g > a)
        {
            a = g;
        }
        if (b > a)
        {
            a = b;
        }
        any = any || (a != 0);
        pixels[i] = (a << 24) | 0x00FFFFFF;
    }

    // Nothing drawn: XFONT can't draw to memory here, keep using it directly
    if (!any || FAILED(m_pDevice->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, 0, &atlas.texture)))
    {
        atlas.texture = NULL;
        free(pixels);
        return false;
    }

    D3DLOCKED_RECT locked;
    if (FAILED(atlas.texture->LockRect(0, &locked, NULL, 0)))
    {
        atlas.texture->Release();
        atlas.texture = NULL;
        free(pixels);
        return false;
    }
    // Textures are swizzled on the Xbox
    XGSwizzleRect(pixels, 0, NULL, locked.pBits, width, height, NULL, sizeof(DWORD));
    atlas.texture->UnlockRect(0);

    free(pixels);
    return true;
}

void Renderer::Shutdown()
{
    if (m_pBackgroundTexture)
    {
        m_pBackgroundTexture->Release();
        m_pBackgroundTexture = NULL;
    }
    m_BackgroundTextureTheme = -1;

    for (int i = 0; i < FONT_COUNT; i++)
    {
        if (m_Atlases[i].texture)
        {
            m_Atlases[i].texture->Release();
            m_Atlases[i].texture = NULL;
        }

        if (m_pFonts[i])
        {
            m_pFonts[i]->Release();
            m_pFonts[i] = NULL;
        }
    }

    if (m_pDevice)
    {
        m_pDevice->Release();
        m_pDevice = NULL;
    }

    if (m_pD3D)
    {
        m_pD3D->Release();
        m_pD3D = NULL;
    }
}

LPCWSTR Renderer::GetVideoModeName() const
{
    switch (m_VideoMode)
    {
    case VIDEO_MODE_720P:
        return L"720p";
    case VIDEO_MODE_480P:
        return L"480p";
    default:
        return L"480i";
    }
}

void Renderer::BeginFrame()
{
    FlushText();
    m_pDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, GetTheme().background, 1.0f, 0);

    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_pDevice->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 255, 255, 255));
}

void Renderer::EndFrame()
{
    FlushText();
    m_pDevice->Present(NULL, NULL, NULL, NULL);
}

void Renderer::Text(Font font, LPCWSTR text, int x, int y, Color color, int align)
{
    if (font < 0 || font >= FONT_COUNT)
    {
        font = FONT_TEXT;
    }
    if (color < 0 || color >= COLOR_COUNT)
    {
        color = COLOR_LIGHT_GREY;
    }

    // The fonts are already sized for the video mode, only the offset is added
    long screen_x = (long)(ToScreenX((FLOAT)x) + 0.5f);
    const long screen_y = (long)(ToScreenY((FLOAT)y) + 0.5f);

    const FontAtlas &atlas = m_Atlases[font];
    if (atlas.texture == NULL)
    {
        // XFONT draws with the CPU: the GPU text before it goes first
        FlushText();
        TextXFont(font, text, screen_x, screen_y, color, align);
        return;
    }

    // Aligned like XFONT: x is the left edge, the middle or the right edge
    const int width = TextWidth(font, text);
    if ((align & XFONT_CENTER) == XFONT_CENTER)
    {
        screen_x -= width / 2;
    }
    else if (align & XFONT_RIGHT)
    {
        screen_x -= width;
    }

    const Theme &theme = GetTheme();
    const int cell = m_FontHeights[font];
    if (g_ColorOpaque[color])
    {
        ScreenRect((FLOAT)screen_x, (FLOAT)screen_y, (FLOAT)(screen_x + width), (FLOAT)(screen_y + cell),
                   theme.text_background, theme.text_background);
    }

    if (m_TextBatchFont != font)
    {
        FlushText();
        m_TextBatchFont = font;
    }

    // Two triangles per character, its slot placed pad pixels left of the pen
    const D3DCOLOR diffuse = theme.text[color];
    const FLOAT tex_w = (FLOAT)atlas.width;
    const FLOAT tex_h = (FLOAT)atlas.height;
    long pen = screen_x;
    for (int i = 0; text[i] != L'\0' && i < TEXT_MAX_CHARS; i++)
    {
        const int g = glyph_index(text[i]);
        if (text[i] != L' ')
        {
            if (m_TextBatchCount + 6 > TEXT_BATCH_VERTICES)
            {
                FlushText();
                m_TextBatchFont = font;
            }

            const int slot = atlas.advance[g] + 2 * atlas.pad;
            // -0.5: pixel centers, like ScreenRect
            const FLOAT x1 = (FLOAT)(pen - atlas.pad) - 0.5f;
            const FLOAT y1 = (FLOAT)screen_y - 0.5f;
            const FLOAT x2 = x1 + slot;
            const FLOAT y2 = y1 + cell;
            const FLOAT u1 = atlas.u[g] / tex_w;
            const FLOAT v1 = atlas.v[g] / tex_h;
            const FLOAT u2 = (atlas.u[g] + slot) / tex_w;
            const FLOAT v2 = (atlas.v[g] + cell) / tex_h;

            TextVertex *q = &g_TextBatch[m_TextBatchCount];
            q[0].pos = D3DXVECTOR4(x1, y1, 1.0f, 1.0f); q[0].u = u1; q[0].v = v1;
            q[1].pos = D3DXVECTOR4(x2, y1, 1.0f, 1.0f); q[1].u = u2; q[1].v = v1;
            q[2].pos = D3DXVECTOR4(x1, y2, 1.0f, 1.0f); q[2].u = u1; q[2].v = v2;
            q[3].pos = D3DXVECTOR4(x2, y1, 1.0f, 1.0f); q[3].u = u2; q[3].v = v1;
            q[4].pos = D3DXVECTOR4(x2, y2, 1.0f, 1.0f); q[4].u = u2; q[4].v = v2;
            q[5].pos = D3DXVECTOR4(x1, y2, 1.0f, 1.0f); q[5].u = u1; q[5].v = v2;
            for (int k = 0; k < 6; k++)
            {
                q[k].color = diffuse;
            }
            m_TextBatchCount += 6;
        }
        pen += atlas.advance[g];
    }
}

void Renderer::FlushText()
{
    if (m_TextBatchCount == 0)
    {
        return;
    }
    const FontAtlas &atlas = m_Atlases[m_TextBatchFont];

    // Texture times the text color, blended by the coverage; pixel exact
    m_pDevice->SetTexture(0, atlas.texture);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    m_pDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    m_pDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    m_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    m_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_pDevice->SetVertexShader(D3DFVF_TEXTVERTEX);
    m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, m_TextBatchCount / 3, g_TextBatch, sizeof(TextVertex));
    m_pDevice->SetTexture(0, NULL);
    m_TextBatchCount = 0;
}

int Renderer::TextWidth(Font font, LPCWSTR text) const
{
    if (font < 0 || font >= FONT_COUNT)
    {
        font = FONT_TEXT;
    }

    const FontAtlas &atlas = m_Atlases[font];
    if (atlas.texture == NULL)
    {
        unsigned width = 0;
        if (m_pFonts[font] == NULL || FAILED(m_pFonts[font]->GetTextExtent(text, (unsigned)-1, &width)))
        {
            return 0;
        }
        return (int)width;
    }

    int width = 0;
    for (int i = 0; text[i] != L'\0' && i < TEXT_MAX_CHARS; i++)
    {
        width += atlas.advance[glyph_index(text[i])];
    }
    return width;
}

void Renderer::TextXFont(Font font, LPCWSTR text, long screen_x, long screen_y, Color color, int align)
{
    XFONT *pFont = m_pFonts[font];
    if (pFont == NULL)
    {
        return;
    }

    const Theme &theme = GetTheme();
    if (g_ColorOpaque[color])
    {
        pFont->SetBkMode(XFONT_OPAQUE);
        pFont->SetBkColor(theme.text_background);
    }
    else
    {
        pFont->SetBkMode(XFONT_TRANSPARENT);
    }
    pFont->SetTextColor(theme.text[color]);
    pFont->SetTextAlignment(XFONT_TOP | align);

    LPDIRECT3DSURFACE8 pBackBuffer;
    m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    pFont->TextOut(pBackBuffer, text, -1, screen_x, screen_y);
    pBackBuffer->Release();
}

int Renderer::FontHeight(Font font) const
{
    return (font >= 0 && font < FONT_COUNT) ? m_FontHeights[font] : 0;
}

int Renderer::FontDescent(Font font) const
{
    return (font >= 0 && font < FONT_COUNT) ? m_FontDescents[font] : 0;
}

int Renderer::FontCapHeight(Font font) const
{
    return (font >= 0 && font < FONT_COUNT) ? m_FontCapHeights[font] : 0;
}

void Renderer::Rect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color)
{
    ScreenRect(ToScreenX(x1), ToScreenY(y1), ToScreenX(x2), ToScreenY(y2), top_color, bottom_color);
}

void Renderer::Outline(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR color)
{
    Rect(x1, y1, x2, y1 + 1.0f, color, color);         // Top
    Rect(x1, y2 - 1.0f, x2, y2, color, color);         // Bottom
    Rect(x1, y1 + 1.0f, x1 + 1.0f, y2 - 1.0f, color, color); // Left
    Rect(x2 - 1.0f, y1 + 1.0f, x2, y2 - 1.0f, color, color); // Right
}

// After a pause (a slow operation), carry on instead of jumping
static const DWORD BACKGROUND_MAX_STEP_MS = 250;

void Renderer::DrawBackground()
{
    FlushText();

    static FLOAT xy[BACKGROUND_VERTEX_COUNT * 2];
    static FLOAT uv[BACKGROUND_VERTEX_COUNT * 2];
    static TextVertex vertices[BACKGROUND_VERTEX_COUNT];
    static DWORD gradient[BACKGROUND_GRADIENT_WIDTH * BACKGROUND_GRADIENT_HEIGHT];

    // The gradient, again when the theme changed
    const Theme &theme = GetTheme();
    if (m_BackgroundTextureTheme != m_ThemeIndex)
    {
        build_background_gradient(theme.background, theme.map_standard, gradient);
        if (m_pBackgroundTexture == NULL &&
            FAILED(m_pDevice->CreateTexture(BACKGROUND_GRADIENT_WIDTH, BACKGROUND_GRADIENT_HEIGHT, 1, 0, D3DFMT_A8R8G8B8, 0,
                                            &m_pBackgroundTexture)))
        {
            m_pBackgroundTexture = NULL;
        }
        D3DLOCKED_RECT locked;
        if (m_pBackgroundTexture != NULL && SUCCEEDED(m_pBackgroundTexture->LockRect(0, &locked, NULL, 0)))
        {
            XGSwizzleRect(gradient, 0, NULL, locked.pBits, BACKGROUND_GRADIENT_WIDTH, BACKGROUND_GRADIENT_HEIGHT, NULL,
                          sizeof(DWORD));
            m_pBackgroundTexture->UnlockRect(0);
        }
        m_BackgroundTextureTheme = m_ThemeIndex;
    }

    // Its own clock: a long gap (a slow operation) counts as one short step
    const DWORD now = GetTickCount();
    if (m_BackgroundTick != 0)
    {
        DWORD elapsed = now - m_BackgroundTick;
        if (elapsed > BACKGROUND_MAX_STEP_MS)
        {
            elapsed = BACKGROUND_MAX_STEP_MS;
        }
        m_BackgroundSeconds += elapsed / 1000.0;
    }
    m_BackgroundTick = now;

    const int count = build_background_mesh((FLOAT)m_Width, (FLOAT)m_Height, m_BackgroundSeconds, xy, uv);

    for (int i = 0; i < count; i++)
    {
        TextVertex &vertex = vertices[i];
        vertex.pos = D3DXVECTOR4(xy[i * 2] - 0.5f, xy[i * 2 + 1] - 0.5f, 1.0f, 1.0f);
        vertex.u = uv[i * 2];
        vertex.v = uv[i * 2 + 1];
        // Without the texture: the corner's gradient color, plainly rounded
        // (the middle dither row)
        int texel = (int)(uv[i * 2] * BACKGROUND_GRADIENT_WIDTH);
        texel = (texel < 0) ? 0 : (texel >= BACKGROUND_GRADIENT_WIDTH) ? BACKGROUND_GRADIENT_WIDTH - 1 : texel;
        vertex.color = gradient[(BACKGROUND_DITHER_ROWS / 2) * BACKGROUND_GRADIENT_WIDTH + texel];
    }

    if (m_pBackgroundTexture != NULL)
    {
        // The gradient for every pixel: exact texels (the dithering is in
        // them), the dither rows repeating down the texture
        m_pDevice->SetTexture(0, m_pBackgroundTexture);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        m_pDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        m_pDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        m_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        m_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    }
    else
    {
        m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    }
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    m_pDevice->SetVertexShader(D3DFVF_TEXTVERTEX);
    m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, vertices, sizeof(TextVertex));
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetTexture(0, NULL);
}

void Renderer::FillScreen(D3DCOLOR color)
{
    ScreenRect(0.0f, 0.0f, (FLOAT)m_Width, (FLOAT)m_Height, color, color);
}

void Renderer::ScreenRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color)
{
    FlushText();

    ScreenVertex v[4];
    v[0].pos = D3DXVECTOR4(x1 - 0.5f, y1 - 0.5f, 1.0f, 1.0f);
    v[0].color = top_color;
    v[1].pos = D3DXVECTOR4(x2 - 0.5f, y1 - 0.5f, 1.0f, 1.0f);
    v[1].color = top_color;
    v[2].pos = D3DXVECTOR4(x1 - 0.5f, y2 - 0.5f, 1.0f, 1.0f);
    v[2].color = bottom_color;
    v[3].pos = D3DXVECTOR4(x2 - 0.5f, y2 - 0.5f, 1.0f, 1.0f);
    v[3].color = bottom_color;

    m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    m_pDevice->SetVertexShader(D3DFVF_SCREENVERTEX);
    m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(ScreenVertex));
}

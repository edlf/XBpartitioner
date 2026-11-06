#include "stdafx.h"
#include "ui.h"

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

struct ColorStyle
{
    D3DCOLOR text;
    bool opaque_background; // Draws on a dark grey box
};

// Indexed by the Color enum
static const ColorStyle g_ColorStyles[COLOR_COUNT] = {
    {D3DCOLOR_XRGB(255, 255, 255), false}, // COLOR_WHITE
    {D3DCOLOR_XRGB(255, 40, 40), false},   // COLOR_RED
    {D3DCOLOR_XRGB(20, 255, 20), false},   // COLOR_GREEN
    {D3DCOLOR_XRGB(100, 100, 255), false}, // COLOR_BLUE
    {D3DCOLOR_XRGB(224, 224, 20), false},  // COLOR_YELLOW
    {D3DCOLOR_XRGB(255, 255, 255), true},  // COLOR_WHITE_BG
    {D3DCOLOR_XRGB(255, 20, 20), true},    // COLOR_RED_BG
    {D3DCOLOR_XRGB(140, 140, 140), false}, // COLOR_GREY
    {D3DCOLOR_XRGB(160, 160, 0), false},   // COLOR_DARK_YELLOW
    {D3DCOLOR_XRGB(240, 240, 240), false}, // COLOR_LIGHT_GREY
    {D3DCOLOR_XRGB(255, 30, 20), false},   // COLOR_WARNING
};

static const D3DCOLOR TEXT_BACKGROUND_COLOR = D3DCOLOR_XRGB(40, 40, 40);

struct FontData
{
    const BYTE *data;
    unsigned int size;
};

// Indexed by the Font enum
static const FontData g_Fonts480[FONT_COUNT] = {
    {font_text, sizeof(font_text)},
    {font_table, sizeof(font_table)},
    {font_title, sizeof(font_title)},
};

static const FontData g_Fonts720[FONT_COUNT] = {
    {font_text_720, sizeof(font_text_720)},
    {font_table_720, sizeof(font_table_720)},
    {font_title_720, sizeof(font_title_720)},
};

Renderer::Renderer()
    : m_pD3D(NULL), m_pDevice(NULL), m_VideoMode(VIDEO_MODE_480I),
      m_Width(LAYOUT_WIDTH), m_Height(LAYOUT_HEIGHT), m_Scale(1.0f), m_OffsetX(0.0f), m_OffsetY(0.0f)
{
    for (int i = 0; i < FONT_COUNT; i++)
    {
        m_pFonts[i] = NULL;
    }
}

Renderer::~Renderer()
{
    Shutdown();
}

void Renderer::Init()
{
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));

    // Pick the video mode from the AV pack and the dashboard video settings:
    //   720p - component (HDTV) pack with 720p enabled
    //   480p - component or VGA pack with 480p enabled
    //   480i - everything else (composite, s-video, scart, rf)
    const DWORD video_flags = XGetVideoFlags();
    const DWORD av_pack = XGetAVPack();

    if (av_pack == XC_AV_PACK_HDTV && (video_flags & XC_VIDEO_FLAGS_HDTV_720p))
    {
        m_VideoMode = VIDEO_MODE_720P;
        m_Width = 1280;
        m_Height = 720;
        d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
    }
    else if ((av_pack == XC_AV_PACK_HDTV || av_pack == XC_AV_PACK_VGA) && (video_flags & XC_VIDEO_FLAGS_HDTV_480p))
    {
        m_VideoMode = VIDEO_MODE_480P;
        m_Width = 640;
        m_Height = 480;
        d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE;
    }
    else
    {
        m_VideoMode = VIDEO_MODE_480I;
        m_Width = 640;
        m_Height = 480;
    }

    // The layout is 4:3: scale it to the screen height and center it horizontally
    m_Scale = (FLOAT)m_Height / LAYOUT_HEIGHT;
    m_OffsetX = ((FLOAT)m_Width - LAYOUT_WIDTH * m_Scale) / 2.0f;
    m_OffsetY = 0.0f;

    d3dpp.BackBufferWidth = m_Width;
    d3dpp.BackBufferHeight = m_Height;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    m_pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    m_pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &m_pDevice);

    m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);

    // Fonts are pre-rendered for each mode, so text stays sharp at 720p
    const FontData *fonts = (m_VideoMode == VIDEO_MODE_720P) ? g_Fonts720 : g_Fonts480;
    for (int i = 0; i < FONT_COUNT; i++)
    {
        XFONT_OpenBitmapFontFromMemory(fonts[i].data, fonts[i].size, &m_pFonts[i]);
    }
}

void Renderer::Shutdown()
{
    for (int i = 0; i < FONT_COUNT; i++)
    {
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
    m_pDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

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
    m_pDevice->Present(NULL, NULL, NULL, NULL);
}

void Renderer::Text(Font font, LPCWSTR text, int x, int y, Color color, int align)
{
    XFONT *pFont = m_pFonts[(font >= 0 && font < FONT_COUNT) ? font : FONT_TEXT];
    const ColorStyle &style = g_ColorStyles[(color >= 0 && color < COLOR_COUNT) ? color : COLOR_LIGHT_GREY];

    if (style.opaque_background)
    {
        pFont->SetBkMode(XFONT_OPAQUE);
        pFont->SetBkColor(TEXT_BACKGROUND_COLOR);
    }
    else
    {
        pFont->SetBkMode(XFONT_TRANSPARENT);
    }
    pFont->SetTextColor(style.text);
    pFont->SetTextAlignment(XFONT_TOP | align);

    // The fonts are already sized for the video mode, only the position is mapped
    const long screen_x = (long)(ToScreenX((FLOAT)x) + 0.5f);
    const long screen_y = (long)(ToScreenY((FLOAT)y) + 0.5f);

    LPDIRECT3DSURFACE8 pBackBuffer;
    m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    pFont->TextOut(pBackBuffer, text, -1, screen_x, screen_y);
    pBackBuffer->Release();
}

void Renderer::Rect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color)
{
    ScreenRect(ToScreenX(x1), ToScreenY(y1), ToScreenX(x2), ToScreenY(y2), top_color, bottom_color);
}

void Renderer::FillScreen(D3DCOLOR color)
{
    ScreenRect(0.0f, 0.0f, (FLOAT)m_Width, (FLOAT)m_Height, color, color);
}

void Renderer::FadeOut()
{
    LPDIRECT3DSURFACE8 pFrontBuffer, pBackBuffer;
    m_pDevice->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &pFrontBuffer);
    m_pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

    // Redraw the last frame with an increasingly opaque black overlay
    for (DWORD alpha = 0; alpha < 0xfe; alpha += 2)
    {
        m_pDevice->CopyRects(pFrontBuffer, 0, 0, pBackBuffer, 0);
        FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        EndFrame();
    }

    pFrontBuffer->Release();
    pBackBuffer->Release();
}

void Renderer::ScreenRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, D3DCOLOR top_color, D3DCOLOR bottom_color)
{
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

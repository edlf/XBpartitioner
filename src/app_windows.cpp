// The start and end fades, and what is drawn behind panels.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// Behind every panel: the main screen, and the editor while the number pad
// is open over it
void App::DrawBackdrop()
{
    m_main_screen.Draw();
    m_editor.DrawUnderNumberPad();
}

void App::FadeIn()
{
    for (int alpha = 0xff; alpha > 0x06; alpha -= 0x06)
    {
        m_ui.BeginFrame();
        m_main_screen.Draw();
        m_main_screen.DrawControlsHint();
        m_ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_ui.EndFrame();
    }
}

// The background keeps moving
void App::FadeOut()
{
    for (int alpha = 0x06; alpha < 0xff; alpha += 0x06)
    {
        m_ui.BeginFrame();
        m_main_screen.Draw();
        m_ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_ui.EndFrame();
    }
    m_ui.BeginFrame();
    m_ui.FillScreen(D3DCOLOR_ARGB(0xff, 0, 0, 0));
    m_ui.EndFrame();
}

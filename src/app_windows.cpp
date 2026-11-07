// Dialogs, info panels, menus, and their open and close animation.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// With cancel_label: OK and Cancel (Cancel selected), true for OK.
// Without: just OK, any button closes it and it returns true.
bool App::RunDialog(LPCWSTR title, Color title_color, const LPCWSTR *lines, int line_count, LPCWSTR ok_label, LPCWSTR cancel_label)
{
    const bool has_cancel = (cancel_label != NULL);
    bool ok_selected = !has_cancel; // Cancel is the default

    const int button_height = m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->dialog_button_padding);
    const int height = m_Layout->dialog_title_height + line_count * m_Layout->dialog_line_height + m_Layout->dialog_padding + button_height + m_Layout->dialog_padding;
    const int top = (m_Layout->height - height) / 2;
    const int buttons_y = top + m_Layout->dialog_title_height + line_count * m_Layout->dialog_line_height + m_Layout->dialog_padding;

    const int buttons_width = has_cancel ? 2 * m_Layout->dialog_button_width + m_Layout->dialog_button_gap : m_Layout->dialog_button_width;
    const int ok_x = (m_Layout->width - buttons_width) / 2;
    const int cancel_x = ok_x + m_Layout->dialog_button_width + m_Layout->dialog_button_gap;

    m_WaitForRelease = true;

    AnimatePanelOpen(m_Layout->dialog_left, top, m_Layout->dialog_right, top + height);

    // Every way out closes the box
    const auto close = [&](bool result) {
        AnimatePanelClose(m_Layout->dialog_left, top, m_Layout->dialog_right, top + height);
        return result;
    };

    while (true)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();

        m_Ui.Rect((FLOAT)m_Layout->dialog_left, (FLOAT)top, (FLOAT)m_Layout->dialog_right, (FLOAT)(top + height), Colors().panel, Colors().panel);
        m_Ui.Outline((FLOAT)m_Layout->dialog_left, (FLOAT)top, (FLOAT)m_Layout->dialog_right, (FLOAT)(top + height), Colors().panel_border);
        m_Ui.Text(FONT_TITLE, title, m_Layout->width / 2, top + m_Layout->dialog_title_offset, title_color, XFONT_CENTER);

        const int title_bottom = top + m_Layout->dialog_title_offset + m_Ui.FontHeight(FONT_TITLE);
        const int header_line_y = (title_bottom + top + m_Layout->dialog_title_height) / 2;
        m_Ui.Rect((FLOAT)(m_Layout->dialog_left + m_Layout->menu_selection_inset), (FLOAT)header_line_y,
                  (FLOAT)(m_Layout->dialog_right - m_Layout->menu_selection_inset), (FLOAT)(header_line_y + 1),
                  Colors().panel_line, Colors().panel_line);

        for (int i = 0; i < line_count; i++)
        {
            m_Ui.Text(FONT_TEXT, lines[i], (m_Layout->dialog_left + m_Layout->dialog_text_inset), top + m_Layout->dialog_title_height + i * m_Layout->dialog_line_height, COLOR_LIGHT_GREY);
        }

        for (int button = 0; button < (has_cancel ? 2 : 1); button++)
        {
            const bool is_ok = (button == 0);
            const int x = is_ok ? ok_x : cancel_x;
            const bool selected = (is_ok == ok_selected);
            const D3DCOLOR color = selected ? Colors().selection : Colors().button;

            m_Ui.Rect((FLOAT)x, (FLOAT)buttons_y, (FLOAT)(x + m_Layout->dialog_button_width), (FLOAT)(buttons_y + button_height), color, color);
            m_Ui.Text(FONT_TEXT, is_ok ? ok_label : cancel_label, x + m_Layout->dialog_button_width / 2,
                      m_Ui.TextYCentered(FONT_TEXT, buttons_y, buttons_y + button_height), selected ? COLOR_YELLOW : COLOR_WHITE, XFONT_CENTER);
        }

        m_Ui.EndFrame();

        if (!ReadInput() || (m_AnalogButton < 0 && m_DigitalButtons == 0))
        {
            continue;
        }
        m_WaitForRelease = true;

        const bool cancel = (m_AnalogButton == XINPUT_GAMEPAD_B) ||
                            (m_DigitalButtons & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) != 0;

        if (!has_cancel)
        {
            if (cancel || m_AnalogButton == XINPUT_GAMEPAD_A)
            {
                return close(true);
            }
            continue;
        }

        if (cancel)
        {
            return close(false);
        }

        if (m_DigitalButtons & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT))
        {
            ok_selected = !ok_selected;
            continue;
        }

        if (m_AnalogButton == XINPUT_GAMEPAD_A)
        {
            return close(ok_selected);
        }
    }
}

// Cancel selected to start with. True for OK.
bool App::ConfirmWarning(const LPCWSTR *lines, int line_count, LPCWSTR ok_label)
{
    return RunDialog(L"Warning", COLOR_WARNING, lines, line_count, ok_label, L"Cancel");
}

void App::ShowWarning(const LPCWSTR *lines, int line_count)
{
    RunDialog(L"Warning", COLOR_WARNING, lines, line_count, L"OK", NULL);
}

void App::ShowError(const LPCWSTR* lines, int line_count)
{
    RunDialog(L"Error", COLOR_WARNING, lines, line_count, L"OK", NULL);
}

void App::FadeIn()
{
    for (int alpha = 0xff; alpha > 0x06; alpha -= 0x06)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawControlsHint();
        m_Ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_Ui.EndFrame();
    }
}

// The background keeps moving
void App::FadeOut()
{
    for (int alpha = 0x06; alpha < 0xff; alpha += 0x06)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        m_Ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_Ui.EndFrame();
    }
    m_Ui.BeginFrame();
    m_Ui.FillScreen(D3DCOLOR_ARGB(0xff, 0, 0, 0));
    m_Ui.EndFrame();
}

// Status line message; doesn't wait for a button
void App::ShowStatus(LPCWSTR message, Color color)
{
    m_Ui.BeginFrame();
    DrawMainScreen();
    m_Ui.Text(FONT_TEXT, message, m_Layout->margin_x, m_Layout->status_y, color);
    m_Ui.EndFrame();
}

void App::ShowMessage(LPCWSTR title, Color title_color, LPCWSTR message)
{
    RunDialog(title, title_color, &message, 1, L"OK", NULL);
}

// Shown at least this long, so a fast save doesn't just flicker
static const DWORD BUSY_MIN_MS = 300;

// One centered line in a dialog wide panel
void App::BusyWindowBox(int *top, int *bottom)
{
    const int height = m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->dialog_padding);
    *top = (m_Layout->height - height) / 2;
    *bottom = *top + height;
}

void App::DrawBusyWindow(LPCWSTR message)
{
    int top, bottom;
    BusyWindowBox(&top, &bottom);

    m_Ui.BeginFrame();
    DrawMainScreen();
    m_Ui.Rect((FLOAT)m_Layout->dialog_left, (FLOAT)top, (FLOAT)m_Layout->dialog_right, (FLOAT)bottom, Colors().panel,
              Colors().panel);
    m_Ui.Outline((FLOAT)m_Layout->dialog_left, (FLOAT)top, (FLOAT)m_Layout->dialog_right, (FLOAT)bottom,
                 Colors().panel_border);
    m_Ui.Text(FONT_TEXT, message, m_Layout->width / 2, m_Ui.TextYCentered(FONT_TEXT, top, bottom), COLOR_WHITE,
              XFONT_CENTER);
    m_Ui.EndFrame();
}

// Drawn before the slow work starts, so it stays on screen however long it takes
DWORD App::OpenBusyWindow(LPCWSTR message)
{
    int top, bottom;
    BusyWindowBox(&top, &bottom);
    AnimatePanelOpen(m_Layout->dialog_left, top, m_Layout->dialog_right, bottom);
    DrawBusyWindow(message);
    return GetTickCount();
}

void App::CloseBusyWindow(LPCWSTR message, DWORD opened)
{
    while (GetTickCount() - opened < BUSY_MIN_MS)
    {
        DrawBusyWindow(message);
    }

    int top, bottom;
    BusyWindowBox(&top, &bottom);
    AnimatePanelClose(m_Layout->dialog_left, top, m_Layout->dialog_right, bottom);
}

void App::ShowInfoPanel(LPCWSTR title, const InfoRow *rows, int row_count)
{
    const int bar_height = MenuItemBarHeight();
    const int item_height = MenuItemHeight();
    const int height = m_Layout->menu_first_item_offset + row_count * item_height + m_Layout->menu_footer_height;
    const int top = (m_Layout->height - height) / 2;
    const int bottom = top + height;
    const int first_item_y = top + m_Layout->menu_first_item_offset;
    const int label_x = m_Layout->info_left + m_Layout->menu_text_inset;
    const int value_x = m_Layout->info_left + m_Layout->info_value_offset;

    // The button that opened it may still be held
    m_WaitForRelease = true;

    AnimatePanelOpen(m_Layout->info_left, top, m_Layout->info_right, bottom);

    while (true)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawPanel(m_Layout->info_left, top, m_Layout->info_right, bottom, title, first_item_y);

        for (int i = 0; i < row_count; i++)
        {
            const int bar_top = first_item_y + i * item_height;
            const int text_y = m_Ui.TextYCentered(FONT_TEXT, bar_top, bar_top + bar_height);
            m_Ui.Text(FONT_TEXT, rows[i].label, label_x, text_y, COLOR_GREY);
            m_Ui.Text(FONT_TEXT, rows[i].value, value_x, text_y, rows[i].color);
        }

        DrawPanelFooter(m_Layout->info_left, m_Layout->info_right, bottom, L"Press any button to close");
        m_Ui.EndFrame();

        if (ReadInput() && (m_AnalogButton >= 0 || m_DigitalButtons != 0))
        {
            m_WaitForRelease = true;
            AnimatePanelClose(m_Layout->info_left, top, m_Layout->info_right, bottom);
            return;
        }
    }
}

// The shared panel: background, border, title and a line under it
void App::DrawPanel(int left, int top, int right, int bottom, LPCWSTR title, int first_item_y)
{
    m_Ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel, Colors().panel);
    m_Ui.Outline((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel_border);
    m_Ui.Text(FONT_TEXT, title, (left + right) / 2, top + m_Layout->menu_title_offset, COLOR_WHITE, XFONT_CENTER);

    const int title_bottom = top + m_Layout->menu_title_offset + m_Ui.FontHeight(FONT_TEXT);
    const int header_line_y = (title_bottom + first_item_y) / 2;
    m_Ui.Rect((FLOAT)(left + m_Layout->menu_selection_inset), (FLOAT)header_line_y,
              (FLOAT)(right - m_Layout->menu_selection_inset), (FLOAT)(header_line_y + 1),
              Colors().panel_line, Colors().panel_line);
}

void App::DrawPanelFooter(int left, int right, int bottom, LPCWSTR text, Color color)
{
    m_Ui.Text(FONT_TEXT, text, (left + right) / 2, bottom - m_Layout->menu_footer_height + m_Layout->menu_footer_text_offset, color, XFONT_CENTER);
}

// More items scroll, with a scroll bar
static const int MENU_MAX_VISIBLE_ITEMS = 8;

// Open or close time
static const DWORD PANEL_ANIMATION_MS = 150;

// Opening grows the empty panel from a line across its middle, closing
// shrinks it back. Timed, so it takes as long in 480 and 720. Off with
// animations=0.
void App::AnimatePanelOpen(int left, int top, int right, int bottom)
{
    AnimatePanel(left, top, right, bottom, true);
}

void App::AnimatePanelClose(int left, int top, int right, int bottom)
{
    AnimatePanel(left, top, right, bottom, false);
}

void App::AnimatePanel(int left, int top, int right, int bottom, bool opening)
{
    if (!m_Animations)
    {
        return;
    }

    const FLOAT center = (top + bottom) / 2.0f;
    const FLOAT full_half = (bottom - top) / 2.0f;
    const DWORD start = GetTickCount();

    while (true)
    {
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= PANEL_ANIMATION_MS)
        {
            return;
        }

        // How far open, 0 to 1; closing runs backwards
        FLOAT t = (FLOAT)elapsed / (FLOAT)PANEL_ANIMATION_MS;
        if (!opening)
        {
            t = 1.0f - t;
        }
        const FLOAT eased = 1.0f - (1.0f - t) * (1.0f - t);
        FLOAT half = full_half * eased;
        if (half < 0.5f)
        {
            half = 0.5f; // A one pixel line at the start (and the end of closing)
        }

        m_Ui.BeginFrame();
        DrawMainScreen();
        // The number pad opens over the editor, which stays behind it
        if (m_pActiveEditView != NULL)
        {
            DrawEditPanel(*m_pActiveEditView);
        }
        m_Ui.Rect((FLOAT)left, center - half, (FLOAT)right, center + half, Colors().panel, Colors().panel);
        m_Ui.Outline((FLOAT)left, center - half, (FLOAT)right, center + half, Colors().panel_border);
        m_Ui.EndFrame();
    }
}

// First item to show so the selected one is in view, moving as little as possible
static int menu_scroll_to(int selected, int first_visible, int visible)
{
    if (selected < first_visible)
    {
        return selected;
    }
    if (selected >= first_visible + visible)
    {
        return selected - visible + 1;
    }
    return first_visible;
}

// Top of the scroll bar's thumb for the first item shown
static FLOAT menu_thumb_top(int track_top, int track_height, int thumb_height, int first_visible, int count, int visible)
{
    if (count <= visible)
    {
        return (FLOAT)track_top;
    }
    return (FLOAT)track_top + (FLOAT)((track_height - thumb_height) * first_visible) / (FLOAT)(count - visible);
}

// Up/down moves (skipping disabled items), A selects, B returns
// MENU_RESULT_BACK, Start MENU_RESULT_CLOSE. submenu: the footer says "B: back".
int App::RunMenu(LPCWSTR title, const LPCWSTR *labels, const bool *enabled, int count, int initial, bool submenu,
                 MenuPreview preview)
{
    int selected = (initial >= 0 && initial < count) ? initial : 0;
    for (int tries = 0; tries < count && !enabled[selected]; tries++)
    {
        selected = (selected + 1) % count;
    }
    if (!enabled[selected])
    {
        return MENU_RESULT_BACK;
    }
    // What a preview shows again when the menu is cancelled
    const int start_item = selected;

    const FLOAT box_left = (FLOAT)m_Layout->menu_left;
    const FLOAT box_right = (FLOAT)m_Layout->menu_right;
    const int bar_height = MenuItemBarHeight();
    const int item_height = MenuItemHeight();
    const int visible = (count > MENU_MAX_VISIBLE_ITEMS) ? MENU_MAX_VISIBLE_ITEMS : count;
    const bool scrolls = (visible < count);
    const int box_top = m_Layout->menu_top;
    const int box_height = m_Layout->menu_first_item_offset + visible * item_height + m_Layout->menu_footer_height;
    const int first_item_y = box_top + m_Layout->menu_first_item_offset;
    const int box_bottom = box_top + box_height;
    int previewed = -1;

    // The selection bars stop short of the scroll bar
    int first_visible = 0;
    const int scroll_width = (m_Layout->menu_selection_inset + 1) / 2;
    const FLOAT scroll_right = box_right - m_Layout->menu_selection_inset;
    const FLOAT scroll_left = scroll_right - scroll_width;
    const FLOAT selection_right = scrolls ? scroll_left - m_Layout->menu_selection_inset / 2 : scroll_right;
    const int track_top = first_item_y;
    const int track_height = visible * item_height - m_Layout->menu_item_gap;
    const int thumb_height = scrolls ? (track_height * visible + count / 2) / count : track_height;

    first_visible = menu_scroll_to(selected, first_visible, visible);

    FLOAT bar_y = (FLOAT)(first_item_y + (selected - first_visible) * item_height);
    FLOAT thumb_y = menu_thumb_top(track_top, track_height, thumb_height, first_visible, count, visible);
    DWORD slide_tick = 0;
    DWORD thumb_tick = 0;
    DpadRepeat repeat;

    m_WaitForRelease = true;

    AnimatePanelOpen(m_Layout->menu_left, box_top, m_Layout->menu_right, box_bottom);

    // Every way out closes the box. Cancelled with a preview: the start item is
    // previewed again first, so it closes as it was (e.g. the original theme).
    const auto close = [&](int result) {
        if (result < 0 && preview != NULL && previewed != start_item)
        {
            (this->*preview)(start_item);
        }
        AnimatePanelClose(m_Layout->menu_left, box_top, m_Layout->menu_right, box_bottom);
        return result;
    };

    while (true)
    {
        if (preview != NULL && previewed != selected)
        {
            (this->*preview)(selected);
            previewed = selected;
        }

        first_visible = menu_scroll_to(selected, first_visible, visible);
        SlideTowards(bar_y, (FLOAT)(first_item_y + (selected - first_visible) * item_height), slide_tick);
        SlideTowards(thumb_y, menu_thumb_top(track_top, track_height, thumb_height, first_visible, count, visible), thumb_tick);

        m_Ui.BeginFrame();
        DrawMainScreen();

        DrawPanel(m_Layout->menu_left, box_top, m_Layout->menu_right, box_bottom, title, first_item_y);

        // Before the text, so it never covers an item while moving
        const int selection_top = (int)(bar_y + 0.5f);
        m_Ui.Rect(box_left + m_Layout->menu_selection_inset, (FLOAT)selection_top,
                  selection_right, (FLOAT)(selection_top + bar_height),
                  Colors().selection, Colors().selection);

        if (scrolls)
        {
            const int thumb_top = (int)(thumb_y + 0.5f);
            m_Ui.Rect(scroll_left, (FLOAT)track_top, scroll_right, (FLOAT)(track_top + track_height),
                      Colors().button, Colors().button);
            m_Ui.Rect(scroll_left, (FLOAT)thumb_top, scroll_right, (FLOAT)(thumb_top + thumb_height),
                      Colors().panel_border, Colors().panel_border);
        }

        for (int i = first_visible; i < first_visible + visible; i++)
        {
            const int bar_top = first_item_y + (i - first_visible) * item_height;
            const int bar_bottom = bar_top + bar_height;
            Color color = enabled[i] ? COLOR_WHITE : COLOR_GREY;

            if (i == selected)
            {
                color = COLOR_YELLOW;
            }

            m_Ui.Text(FONT_TEXT, labels[i], m_Layout->menu_left + m_Layout->menu_text_inset,
                      m_Ui.TextYCentered(FONT_TEXT, bar_top, bar_bottom), color);
        }

        DrawPanelFooter(m_Layout->menu_left, m_Layout->menu_right, box_bottom, submenu ? L"A: select    B: back    Start: close" : L"A: select    B: close");
        m_Ui.EndFrame();

        if (!ReadInput())
        {
            repeat.Step(0);
            continue;
        }

        const int direction = (m_DigitalButtons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (m_DigitalButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
        const int move = repeat.Step(direction);
        if (move != 0)
        {
            // At least one item is enabled, so this ends
            const int step = (move < 0) ? count - 1 : 1;
            do
            {
                selected = (selected + step) % count;
            } while (!enabled[selected]);
            continue;
        }

        if (m_DigitalButtons & XINPUT_GAMEPAD_START)
        {
            m_WaitForRelease = true;
            return close(MENU_RESULT_CLOSE);
        }

        if (m_AnalogButton == XINPUT_GAMEPAD_B)
        {
            m_WaitForRelease = true;
            return close(MENU_RESULT_BACK);
        }

        if (m_AnalogButton == XINPUT_GAMEPAD_A && enabled[selected])
        {
            m_WaitForRelease = true;
            return close(selected);
        }
    }
}

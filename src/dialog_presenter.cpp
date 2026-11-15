#include "stdafx.h"
#include "dialog_presenter.h"
#include "array_length.h"
#include <stdio.h>
#include <string.h>

DialogPresenter::DialogPresenter(Renderer &ui, GamepadInput &input, ScreenBackdrop &backdrop)
    : m_ui(ui), m_input(input), m_backdrop(backdrop), m_layout(&g_Layout480), m_animations(true)
{
}

void DialogPresenter::Configure(const Layout *layout, bool animations)
{
    m_layout = layout;
    m_animations = animations;
}

int DialogPresenter::MenuItemBarHeight() const
{
    return m_ui.TextBoxHeight(FONT_TEXT, m_layout->menu_item_padding);
}

int DialogPresenter::MenuItemHeight() const
{
    return MenuItemBarHeight() + m_layout->menu_item_gap;
}

// With a cancel label: OK and Cancel (Cancel selected), true for OK.
// Without: just OK, any button closes it and it returns true.
bool DialogPresenter::RunDialog(const DialogText &text, const DialogButtons &buttons)
{
    DialogView view = PlaceDialog(text, buttons);
    m_input.WaitForRelease();
    AnimatePanelOpen(view.box);

    DialogAnswer answer = DIALOG_OPEN;
    while (answer == DIALOG_OPEN)
    {
        DrawDialog(text, buttons, view);
        if (m_input.Read() && m_input.AnyPressed())
        {
            m_input.WaitForRelease();
            answer = HandleDialogButtons(buttons, view);
        }
    }

    // Every way out closes the box
    AnimatePanelClose(view.box);
    return answer == DIALOG_OK;
}

// Centred, as tall as its lines; Cancel is selected to start with
DialogPresenter::DialogView DialogPresenter::PlaceDialog(const DialogText &text, const DialogButtons &buttons) const
{
    const bool has_cancel = (buttons.cancel_label != NULL);
    const int lines_height = m_layout->dialog_title_height + text.line_count * m_layout->dialog_line_height + m_layout->dialog_padding;

    DialogView view;
    view.button_height = m_ui.TextBoxHeight(FONT_TEXT, m_layout->dialog_button_padding);
    const int height = lines_height + view.button_height + m_layout->dialog_padding;
    const int top = (m_layout->height - height) / 2;
    const PanelRect box = {m_layout->dialog_left, top, m_layout->dialog_right, top + height};
    view.box = box;
    view.buttons_y = top + lines_height;

    const int buttons_width = has_cancel ? 2 * m_layout->dialog_button_width + m_layout->dialog_button_gap : m_layout->dialog_button_width;
    view.ok_x = (m_layout->width - buttons_width) / 2;
    view.cancel_x = view.ok_x + m_layout->dialog_button_width + m_layout->dialog_button_gap;
    view.ok_selected = !has_cancel;
    return view;
}

void DialogPresenter::DrawDialog(const DialogText &text, const DialogButtons &buttons, const DialogView &view)
{
    const PanelRect &box = view.box;
    m_ui.BeginFrame();
    m_backdrop.DrawBackdrop();

    m_ui.Rect((FLOAT)box.left, (FLOAT)box.top, (FLOAT)box.right, (FLOAT)box.bottom, Colors().panel, Colors().panel);
    m_ui.Outline((FLOAT)box.left, (FLOAT)box.top, (FLOAT)box.right, (FLOAT)box.bottom, Colors().panel_border);
    m_ui.Text(FONT_TITLE, text.title, m_layout->width / 2, box.top + m_layout->dialog_title_offset, text.title_color, XFONT_CENTER);

    const int title_bottom = box.top + m_layout->dialog_title_offset + m_ui.FontHeight(FONT_TITLE);
    const int header_line_y = (title_bottom + box.top + m_layout->dialog_title_height) / 2;
    m_ui.Rect((FLOAT)(box.left + m_layout->menu_selection_inset),
              (FLOAT)header_line_y,
              (FLOAT)(box.right - m_layout->menu_selection_inset),
              (FLOAT)(header_line_y + 1),
              Colors().panel_line,
              Colors().panel_line);

    for (int i = 0; i < text.line_count; i++)
    {
        const int y = box.top + m_layout->dialog_title_height + i * m_layout->dialog_line_height;
        m_ui.Text(FONT_TEXT, text.lines[i], box.left + m_layout->dialog_text_inset, y, COLOR_LIGHT_GREY);
    }

    DrawDialogButtons(buttons, view);
    m_ui.EndFrame();
}

void DialogPresenter::DrawDialogButtons(const DialogButtons &buttons, const DialogView &view)
{
    const int count = (buttons.cancel_label != NULL) ? 2 : 1;
    for (int button = 0; button < count; button++)
    {
        const bool is_ok = (button == 0);
        const int x = is_ok ? view.ok_x : view.cancel_x;
        const int y = view.buttons_y;
        const bool selected = (is_ok == view.ok_selected);
        const D3DCOLOR color = selected ? Colors().selection : Colors().button;

        m_ui.Rect((FLOAT)x, (FLOAT)y, (FLOAT)(x + m_layout->dialog_button_width), (FLOAT)(y + view.button_height), color, color);
        m_ui.Text(FONT_TEXT,
                  is_ok ? buttons.ok_label : buttons.cancel_label,
                  x + m_layout->dialog_button_width / 2,
                  m_ui.TextYCentered(FONT_TEXT, y, y + view.button_height),
                  selected ? COLOR_YELLOW : COLOR_WHITE,
                  XFONT_CENTER);
    }
}

// Only OK: A, B, Back or Start close it. With Cancel: Left/Right pick, A
// answers, B, Back or Start cancel.
DialogPresenter::DialogAnswer DialogPresenter::HandleDialogButtons(const DialogButtons &buttons, DialogView &view)
{
    const int analog = m_input.AnalogButton();
    const int digital = m_input.DigitalButtons();
    const bool cancel = (analog == XINPUT_GAMEPAD_B) || (digital & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) != 0;

    if (buttons.cancel_label == NULL)
    {
        return (cancel || analog == XINPUT_GAMEPAD_A) ? DIALOG_OK : DIALOG_OPEN;
    }
    if (cancel)
    {
        return DIALOG_CANCEL;
    }
    if (digital & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT))
    {
        view.ok_selected = !view.ok_selected;
        return DIALOG_OPEN;
    }
    if (analog == XINPUT_GAMEPAD_A)
    {
        return view.ok_selected ? DIALOG_OK : DIALOG_CANCEL;
    }
    return DIALOG_OPEN;
}

// Cancel selected to start with. True for OK.
bool DialogPresenter::ConfirmWarning(const LPCWSTR *lines, int line_count, LPCWSTR ok_label)
{
    return RunDialog(DialogText{L"Warning", COLOR_WARNING, lines, line_count}, DialogButtons{ok_label, L"Cancel"});
}

void DialogPresenter::ShowWarning(const LPCWSTR *lines, int line_count)
{
    RunDialog(DialogText{L"Warning", COLOR_WARNING, lines, line_count}, DIALOG_OK_ONLY);
}

void DialogPresenter::ShowError(const LPCWSTR *lines, int line_count)
{
    RunDialog(DialogText{L"Error", COLOR_WARNING, lines, line_count}, DIALOG_OK_ONLY);
}

void DialogPresenter::ShowMessage(LPCWSTR title, Color title_color, LPCWSTR message)
{
    RunDialog(DialogText{title, title_color, &message, 1}, DIALOG_OK_ONLY);
}

// Shown at least this long, so a fast save doesn't just flicker
static const DWORD BUSY_MIN_MS = 300;

// One centered line in a dialog wide panel
PanelRect DialogPresenter::BusyWindowBox()
{
    const int height = m_ui.TextBoxHeight(FONT_TEXT, m_layout->dialog_padding);
    const int top = (m_layout->height - height) / 2;
    const PanelRect box = {m_layout->dialog_left, top, m_layout->dialog_right, top + height};
    return box;
}

void DialogPresenter::DrawBusyWindow(LPCWSTR message)
{
    const PanelRect box = BusyWindowBox();

    m_ui.BeginFrame();
    m_backdrop.DrawBackdrop();
    m_ui.Rect((FLOAT)box.left, (FLOAT)box.top, (FLOAT)box.right, (FLOAT)box.bottom, Colors().panel, Colors().panel);
    m_ui.Outline((FLOAT)box.left, (FLOAT)box.top, (FLOAT)box.right, (FLOAT)box.bottom, Colors().panel_border);
    m_ui.Text(FONT_TEXT, message, m_layout->width / 2, m_ui.TextYCentered(FONT_TEXT, box.top, box.bottom), COLOR_WHITE, XFONT_CENTER);
    m_ui.EndFrame();
}

// Drawn before the slow work starts, so it stays on screen however long it takes
DWORD DialogPresenter::OpenBusyWindow(LPCWSTR message)
{
    AnimatePanelOpen(BusyWindowBox());
    DrawBusyWindow(message);
    return GetTickCount();
}

// Same as the main thread's stack (imagebld /stack)
static const DWORD BUSY_THREAD_STACK = 64 * 1024;

// Runs routine on a worker thread and keeps drawing the busy window until it
// finishes, so the background keeps moving
void DialogPresenter::RunWithBusyWindow(const BusyWork &work, LPCWSTR message)
{
    const volatile LONG *percent = work.percent;
    const DWORD opened = OpenBusyWindow(message);

    HANDLE thread = CreateThread(NULL, BUSY_THREAD_STACK, work.routine, work.parameter, 0, NULL);
    if (thread != NULL)
    {
        wchar_t text[120];
        while (WaitForSingleObject(thread, 0) == WAIT_TIMEOUT)
        {
            if (percent != NULL)
            {
                _snwprintf(text, ArrayLength(text), L"%s %d%%", message, (int)*percent);
                text[ArrayLength(text) - 1] = L'\0';
                DrawBusyWindow(text);
            }
            else
            {
                DrawBusyWindow(message);
            }
        }
        CloseHandle(thread);
    }
    else
    {
        // No thread available: run it here and let the screen pause
        work.routine(work.parameter);
    }

    CloseBusyWindow(message, opened);
}

void DialogPresenter::CloseBusyWindow(LPCWSTR message, DWORD opened)
{
    while (GetTickCount() - opened < BUSY_MIN_MS)
    {
        DrawBusyWindow(message);
    }

    AnimatePanelClose(BusyWindowBox());
}

void DialogPresenter::ShowInfoPanel(LPCWSTR title, const InfoRow *rows, int row_count)
{
    const int bar_height = MenuItemBarHeight();
    const int item_height = MenuItemHeight();
    const int height = m_layout->menu_first_item_offset + row_count * item_height + m_layout->menu_footer_height;
    const int top = (m_layout->height - height) / 2;
    const PanelRect box = {m_layout->info_left, top, m_layout->info_right, top + height};
    const int first_item_y = top + m_layout->menu_first_item_offset;
    const int label_x = m_layout->info_left + m_layout->menu_text_inset;
    const int value_x = m_layout->info_left + m_layout->info_value_offset;

    // The button that opened it may still be held
    m_input.WaitForRelease();

    AnimatePanelOpen(box);

    while (true)
    {
        m_ui.BeginFrame();
        m_backdrop.DrawBackdrop();
        DrawPanel(box, title, first_item_y);

        for (int i = 0; i < row_count; i++)
        {
            const int bar_top = first_item_y + i * item_height;
            const int text_y = m_ui.TextYCentered(FONT_TEXT, bar_top, bar_top + bar_height);
            m_ui.Text(FONT_TEXT, rows[i].label, label_x, text_y, COLOR_GREY);
            m_ui.Text(FONT_TEXT, rows[i].value, value_x, text_y, rows[i].color);
        }

        DrawPanelFooter(box, L"Press any button to close");
        m_ui.EndFrame();

        if (m_input.Read() && m_input.AnyPressed())
        {
            m_input.WaitForRelease();
            AnimatePanelClose(box);
            return;
        }
    }
}

// The shared panel: background, border, title and a line under it
void DialogPresenter::BeginPanelFrame()
{
    m_ui.BeginFrame();
    m_backdrop.DrawBackdrop();
}

void DialogPresenter::DrawPanel(const PanelRect &box, LPCWSTR title, int first_item_y)
{
    const int left = box.left;
    const int top = box.top;
    const int right = box.right;
    const int bottom = box.bottom;
    m_ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel, Colors().panel);
    m_ui.Outline((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel_border);
    m_ui.Text(FONT_TEXT, title, (left + right) / 2, top + m_layout->menu_title_offset, COLOR_WHITE, XFONT_CENTER);

    const int title_bottom = top + m_layout->menu_title_offset + m_ui.FontHeight(FONT_TEXT);
    const int header_line_y = (title_bottom + first_item_y) / 2;
    m_ui.Rect((FLOAT)(left + m_layout->menu_selection_inset),
              (FLOAT)header_line_y,
              (FLOAT)(right - m_layout->menu_selection_inset),
              (FLOAT)(header_line_y + 1),
              Colors().panel_line,
              Colors().panel_line);
}

void DialogPresenter::DrawPanelFooter(const PanelRect &box, LPCWSTR text, Color color)
{
    const int text_y = box.bottom - m_layout->menu_footer_height + m_layout->menu_footer_text_offset;
    m_ui.Text(FONT_TEXT, text, (box.left + box.right) / 2, text_y, color, XFONT_CENTER);
}

// Open or close time
static const DWORD PANEL_ANIMATION_MS = 150;

// Timed, so it takes as long in 480 and 720
void DialogPresenter::AnimatePanelOpen(const PanelRect &box)
{
    AnimatePanel(box, PANEL_OPENING);
}

void DialogPresenter::AnimatePanelClose(const PanelRect &box)
{
    AnimatePanel(box, PANEL_CLOSING);
}

void DialogPresenter::AnimatePanel(const PanelRect &box, PanelAnimation animation)
{
    if (!m_animations)
    {
        return;
    }

    const FLOAT center = (box.top + box.bottom) / 2.0f;
    const FLOAT full_half = (box.bottom - box.top) / 2.0f;
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
        if (animation == PANEL_CLOSING)
        {
            t = 1.0f - t;
        }
        const FLOAT eased = 1.0f - (1.0f - t) * (1.0f - t);
        FLOAT half = full_half * eased;
        if (half < 0.5f)
        {
            half = 0.5f; // A one pixel line at the start (and the end of closing)
        }

        m_ui.BeginFrame();
        m_backdrop.DrawBackdrop();
        m_ui.Rect((FLOAT)box.left, center - half, (FLOAT)box.right, center + half, Colors().panel, Colors().panel);
        m_ui.Outline((FLOAT)box.left, center - half, (FLOAT)box.right, center + half, Colors().panel_border);
        m_ui.EndFrame();
    }
}


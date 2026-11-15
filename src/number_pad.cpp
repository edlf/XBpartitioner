#include "stdafx.h"
#include "number_pad.h"
#include "array_length.h"
#include <stdio.h>
#include <string.h>

// Number input keys: 0-15 are the digits (A-F only in hex)
enum KeypadKey
{
    KEY_DELETE = 16,
    KEY_UNIT,
    KEY_CLEAR,
    KEY_CANCEL,
    KEY_OK
};

static const int KEYPAD_COLUMNS = 3;

static const int KEYPAD_ROWS = 7;

static const int KEYPAD_HEX_ROWS = 2; // The top rows, A-F: skipped unless typing hex

static const int g_KeypadKeys[KEYPAD_ROWS][KEYPAD_COLUMNS] = {
    {0xA, 0xB, 0xC},
    {0xD, 0xE, 0xF},
    {7, 8, 9},
    {4, 5, 6},
    {1, 2, 3},
    {KEY_UNIT, 0, KEY_DELETE},
    {KEY_CANCEL, KEY_CLEAR, KEY_OK},
};

// A digit is shown as itself, in hex
static LPCWSTR key_label(int key, wchar_t *digit)
{
    static const LPCWSTR names[] = {L"Delete", L"Unit", L"Clear", L"Cancel", L"OK"};
    if (key >= KEY_DELETE)
    {
        return names[key - KEY_DELETE];
    }
    swprintf(digit, L"%X", key);
    return digit;
}

NumberPad::PadState::PadState(int unit)
    : number(unit), row(KEYPAD_HEX_ROWS + 1), column(1), message(NULL), sectors(0) // On the 5
{
}

NumberPad::NumberPad(const ScreenTools &tools) : m_ui(tools.ui), m_input(tools.input), m_dialogs(tools.dialogs), m_layout(&g_Layout480)
{
}

void NumberPad::Configure(const Layout *layout)
{
    m_layout = layout;
}

bool NumberPad::Run(const NumberInputRequest &request, ULONGLONG *result)
{
    PadState pad(request.unit);
    m_input.WaitForRelease();

    // AnimatePanelOpen draws the editor behind it (the dialogs' backdrop)
    const PanelRect box = Box();
    m_dialogs.AnimatePanelOpen(box);

    Outcome outcome = PAD_OPEN;
    while (outcome == PAD_OPEN)
    {
        m_dialogs.BeginPanelFrame();
        Draw(request, pad);
        m_ui.EndFrame();

        if (m_input.Read())
        {
            outcome = HandleButtons(request, pad);
        }
        else
        {
            pad.vertical_repeat.Step(0);
            pad.horizontal_repeat.Step(0);
        }
    }

    m_input.WaitForRelease();
    m_dialogs.AnimatePanelClose(box);
    if (outcome != PAD_OK)
    {
        return false;
    }
    *result = pad.sectors;
    return true;
}

NumberPad::Outcome NumberPad::HandleButtons(const NumberInputRequest &request, PadState &pad)
{
    MoveCursor(pad);
    const int key = PressedKey(pad);
    if (key < 0)
    {
        return PAD_OPEN;
    }

    m_input.WaitForRelease();
    pad.message = NULL;
    return PressKey(request, pad, key);
}

// The D-pad, held; the A-F rows only in hex
void NumberPad::MoveCursor(PadState &pad)
{
    const int buttons = m_input.DigitalButtons();
    const int vertical = pad.vertical_repeat.Step((buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0);
    const int horizontal = pad.horizontal_repeat.Step((buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0);

    const int new_row = pad.row + vertical;
    const int new_column = pad.column + horizontal;
    const int first_row = (InputBase(pad.number.Unit()) == 16) ? 0 : KEYPAD_HEX_ROWS;
    if (new_row >= first_row && new_row < KEYPAD_ROWS)
    {
        pad.row = new_row;
    }
    if (new_column >= 0 && new_column < KEYPAD_COLUMNS)
    {
        pad.column = new_column;
    }
}

// A presses the selected key; X, Y, B and Start are shortcuts. -1 if none.
int NumberPad::PressedKey(const PadState &pad) const
{
    const int analog = m_input.AnalogButton();
    const int buttons = m_input.DigitalButtons();
    if (analog == XINPUT_GAMEPAD_A)
    {
        return g_KeypadKeys[pad.row][pad.column];
    }
    if (analog == XINPUT_GAMEPAD_X)
    {
        return KEY_DELETE;
    }
    if (analog == XINPUT_GAMEPAD_Y)
    {
        return KEY_UNIT;
    }
    if (analog == XINPUT_GAMEPAD_B || (buttons & XINPUT_GAMEPAD_BACK))
    {
        return KEY_CANCEL;
    }
    return (buttons & XINPUT_GAMEPAD_START) ? KEY_OK : -1;
}

NumberPad::Outcome NumberPad::PressKey(const NumberInputRequest &request, PadState &pad, int key)
{
    switch (key)
    {
    case KEY_DELETE:
        pad.number.DeleteDigit();
        break;
    case KEY_CLEAR:
        pad.number.Clear();
        break;
    case KEY_UNIT:
        pad.number.NextUnit();
        if (InputBase(pad.number.Unit()) != 16 && pad.row < KEYPAD_HEX_ROWS)
        {
            pad.row = KEYPAD_HEX_ROWS; // Off the A-F keys
        }
        break;
    case KEY_CANCEL:
        return PAD_CANCELLED;
    case KEY_OK:
        return PressOk(request, pad);
    default:
        if (!pad.number.AddDigit(key))
        {
            pad.message = L"No more digits fit.";
        }
        break;
    }
    return PAD_OPEN;
}

NumberPad::Outcome NumberPad::PressOk(const NumberInputRequest &request, PadState &pad)
{
    const TypedValue value = pad.number.Check(request.min, request.max);
    if (!value.valid)
    {
        memcpy(pad.message_text, value.message, sizeof(pad.message_text));
        pad.message = pad.message_text;
        return PAD_OPEN;
    }
    pad.sectors = value.sectors;
    return PAD_OK;
}

void NumberPad::Draw(const NumberInputRequest &request, const PadState &pad)
{
    const PadGeometry geometry = Geometry();
    const PanelRect box = {geometry.left, geometry.top, geometry.right, geometry.bottom};
    m_dialogs.DrawPanel(box, request.title, geometry.value_y);

    DrawValue(pad, geometry);

    wchar_t text[INPUT_TEXT_COUNT];
    FormatCurrentValue(request.current, pad.number.Unit(), text);
    m_ui.Text(FONT_TEXT, text, geometry.left + geometry.inset, geometry.now_y, COLOR_GREY);
    FormatAllowedRange(request.min, request.max, pad.number.Unit(), text);
    m_ui.Text(FONT_TEXT, text, geometry.left + geometry.inset, geometry.range_y, COLOR_GREY);

    DrawKeys(pad, geometry);

    if (pad.message != NULL)
    {
        m_dialogs.DrawPanelFooter(box, pad.message, COLOR_RED);
    }
    else
    {
        m_dialogs.DrawPanelFooter(box, L"A: press   X: delete   Y: unit   Start: OK   B: cancel");
    }
}

// The digits so far with a cursor, and the unit on the right
void NumberPad::DrawValue(const PadState &pad, const PadGeometry &geometry)
{
    const int left = geometry.left + geometry.inset;
    const int right = geometry.right - geometry.inset;
    const int top = geometry.value_y;
    const int bottom = top + geometry.key_height;
    m_ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().selection, Colors().selection);

    wchar_t text[INPUT_TEXT_COUNT];
    const int unit = pad.number.Unit();
    swprintf(text, (InputBase(unit) == 16) ? L"0x%s_" : L"%s_", pad.number.Digits());
    m_ui.Text(FONT_TABLE, text, left + geometry.gap, m_ui.TextYCentered(FONT_TABLE, top, bottom), COLOR_YELLOW);
    m_ui.Text(FONT_TEXT, InputUnitName(unit), right - geometry.gap, m_ui.TextYCentered(FONT_TEXT, top, bottom), COLOR_WHITE, XFONT_RIGHT);
}

void NumberPad::DrawKeys(const PadState &pad, const PadGeometry &geometry)
{
    const int selected_key = g_KeypadKeys[pad.row][pad.column];
    const int base = InputBase(pad.number.Unit());
    for (int r = 0; r < KEYPAD_ROWS; r++)
    {
        for (int c = 0; c < KEYPAD_COLUMNS; c++)
        {
            const int key = g_KeypadKeys[r][c];
            const int x = geometry.left + geometry.inset + c * (geometry.key_width + geometry.gap);
            const int y = geometry.keys_y + r * (geometry.key_height + geometry.gap);
            const bool selected = (key == selected_key);
            const bool usable = (key > 0xF || key < base); // A-F only in hex
            const D3DCOLOR color = selected ? Colors().selection : Colors().button;

            wchar_t digit[4];
            const LPCWSTR label = key_label(key, digit);
            m_ui.Rect((FLOAT)x, (FLOAT)y, (FLOAT)(x + geometry.key_width), (FLOAT)(y + geometry.key_height), color, color);
            m_ui.Text(FONT_TEXT,
                      label,
                      x + geometry.key_width / 2,
                      m_ui.TextYCentered(FONT_TEXT, y, y + geometry.key_height),
                      selected ? COLOR_YELLOW : (usable ? COLOR_WHITE : COLOR_GREY),
                      XFONT_CENTER);
        }
    }
}

NumberPad::PadGeometry NumberPad::Geometry()
{
    PadGeometry geometry;
    geometry.left = m_layout->dialog_left;
    geometry.right = m_layout->dialog_right;
    geometry.inset = m_layout->dialog_text_inset;
    geometry.gap = m_layout->menu_selection_inset;
    geometry.key_height = m_ui.TextBoxHeight(FONT_TEXT, m_layout->dialog_button_padding);
    geometry.key_width = (geometry.right - geometry.left - 2 * geometry.inset - (KEYPAD_COLUMNS - 1) * geometry.gap) / KEYPAD_COLUMNS;

    const int height = Height();
    geometry.top = (m_layout->height - height) / 2;
    geometry.bottom = geometry.top + height;
    geometry.value_y = geometry.top + m_layout->menu_first_item_offset;
    geometry.now_y = geometry.value_y + geometry.key_height + geometry.gap;
    geometry.range_y = geometry.now_y + m_layout->dialog_line_height;
    geometry.keys_y = geometry.range_y + m_layout->dialog_line_height + geometry.gap;
    return geometry;
}

int NumberPad::Height()
{
    const int gap = m_layout->menu_selection_inset;
    const int key_height = m_ui.TextBoxHeight(FONT_TEXT, m_layout->dialog_button_padding);
    const int line_height = m_layout->dialog_line_height;
    return m_layout->menu_first_item_offset + key_height + gap + 2 * line_height + gap + KEYPAD_ROWS * (key_height + gap) - gap + m_layout->menu_footer_height;
}

PanelRect NumberPad::Box()
{
    const int height = Height();
    const int top = (m_layout->height - height) / 2;
    const PanelRect box = {m_layout->dialog_left, top, m_layout->dialog_right, top + height};
    return box;
}

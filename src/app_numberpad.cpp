// The on screen number pad for typing a start, size or end.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

static const ULONGLONG g_InputUnitSectors[INPUT_UNIT_COUNT] = {1, 1, 0x800, 0x200000};

static const LPCWSTR g_InputUnitNames[INPUT_UNIT_COUNT] = {L"sectors", L"sectors (hex)", L"MB", L"GB"};

static const LPCWSTR g_InputUnitShortNames[INPUT_UNIT_COUNT] = {L"sectors", L"sectors", L"MB", L"GB"};

static int input_base(int unit)
{
    return (unit == INPUT_UNIT_SECTORS_HEX) ? 16 : 10;
}

// A whole number of units as it is typed in that unit: 1234 or 0x4D2
static void format_units(ULONGLONG value, int unit, wchar_t *out)
{
    swprintf(out, (input_base(unit) == 16) ? L"0x%I64X" : L"%I64u", value);
}

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

// Times a unit still fits in 64 bits, also in hex (60 bits)
static const int KEYPAD_MAX_DIGITS = 15;

static ULONGLONG parse_digits(const wchar_t *digits, int length, int base)
{
    ULONGLONG value = 0;
    for (int i = 0; i < length; i++)
    {
        const wchar_t d = digits[i];
        value = value * base + ((d >= L'A') ? (d - L'A' + 10) : (d - L'0'));
    }
    return value;
}

// Whole units within [min, max] sectors; false if there are none
static bool unit_range(ULONGLONG min, ULONGLONG max, int unit, ULONGLONG *min_units, ULONGLONG *max_units)
{
    const ULONGLONG per_unit = g_InputUnitSectors[unit];
    *min_units = min / per_unit + ((min % per_unit) ? 1 : 0);
    *max_units = max / per_unit;
    return *min_units <= *max_units;
}

// D-pad picks a key, A presses it, X deletes, Y changes the unit, Start is
// OK, B cancels. False if cancelled; *result is in sectors, within min..max.
bool App::RunNumberInput(LPCWSTR title, ULONGLONG current, ULONGLONG min, ULONGLONG max, int unit, ULONGLONG *result)
{
    wchar_t digits[KEYPAD_MAX_DIGITS + 1];
    int length = 0;
    digits[0] = L'\0';
    int row = KEYPAD_HEX_ROWS + 1, column = 1; // On the 5
    LPCWSTR message = NULL;
    wchar_t message_text[100];
    DpadRepeat vertical_repeat;
    DpadRepeat horizontal_repeat;
    m_WaitForRelease = true;

    // AnimatePanelOpen draws the editor behind it (m_pActiveEditView)
    const int box_top = (m_Layout->height - NumberInputHeight()) / 2;
    AnimatePanelOpen(m_Layout->dialog_left, box_top, m_Layout->dialog_right, box_top + NumberInputHeight());

    while (true)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        if (m_pActiveEditView != NULL)
        {
            DrawEditPanel(*m_pActiveEditView);
        }
        DrawNumberInput(title, digits, unit, current, min, max, g_KeypadKeys[row][column], message);
        m_Ui.EndFrame();

        if (!ReadInput())
        {
            vertical_repeat.Step(0);
            horizontal_repeat.Step(0);
            continue;
        }
        const int buttons = m_DigitalButtons;
        const int analog = m_AnalogButton;

        const int vertical = vertical_repeat.Step((buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0);
        const int horizontal = horizontal_repeat.Step((buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0);
        if (vertical != 0 || horizontal != 0)
        {
            const int new_row = row + vertical;
            const int new_column = column + horizontal;
            const int first_row = (input_base(unit) == 16) ? 0 : KEYPAD_HEX_ROWS;
            if (new_row >= first_row && new_row < KEYPAD_ROWS)
            {
                row = new_row;
            }
            if (new_column >= 0 && new_column < KEYPAD_COLUMNS)
            {
                column = new_column;
            }
        }

        int key = -1;
        if (analog == XINPUT_GAMEPAD_A)
        {
            key = g_KeypadKeys[row][column];
        }
        else if (analog == XINPUT_GAMEPAD_X)
        {
            key = KEY_DELETE;
        }
        else if (analog == XINPUT_GAMEPAD_Y)
        {
            key = KEY_UNIT;
        }
        else if (analog == XINPUT_GAMEPAD_B || (buttons & XINPUT_GAMEPAD_BACK))
        {
            key = KEY_CANCEL;
        }
        else if (buttons & XINPUT_GAMEPAD_START)
        {
            key = KEY_OK;
        }

        if (key < 0)
        {
            continue;
        }

        m_WaitForRelease = true;
        message = NULL;

        if (key <= 0xF)
        {
            if (key >= input_base(unit))
            {
                continue; // A-F outside hex (not reachable with the D-pad)
            }
            if (length == 1 && digits[0] == L'0')
            {
                length = 0; // No leading zeros
            }
            if (length < KEYPAD_MAX_DIGITS)
            {
                digits[length++] = (wchar_t)((key < 10) ? (L'0' + key) : (L'A' + key - 10));
                digits[length] = L'\0';
            }
            else
            {
                message = L"No more digits fit.";
            }
            continue;
        }

        switch (key)
        {
        case KEY_DELETE:
            if (length > 0)
            {
                digits[--length] = L'\0';
            }
            break;

        case KEY_CLEAR:
            length = 0;
            digits[0] = L'\0';
            break;

        case KEY_UNIT:
        {
            // Between decimal and hex the number is kept, in the other base
            const int old_base = input_base(unit);
            unit = (unit + 1) % INPUT_UNIT_COUNT;
            const int new_base = input_base(unit);
            if (new_base != old_base && length > 0)
            {
                wchar_t converted[24];
                swprintf(converted, (new_base == 16) ? L"%I64X" : L"%I64u", parse_digits(digits, length, old_base));
                if ((int)wcslen(converted) <= KEYPAD_MAX_DIGITS)
                {
                    wcscpy(digits, converted);
                    length = (int)wcslen(digits);
                }
                else
                {
                    length = 0;
                    digits[0] = L'\0';
                }
            }
            if (new_base != 16 && row < KEYPAD_HEX_ROWS)
            {
                row = KEYPAD_HEX_ROWS; // Off the A-F keys
            }
            break;
        }

        case KEY_CANCEL:
            m_WaitForRelease = true;
            AnimatePanelClose(m_Layout->dialog_left, box_top, m_Layout->dialog_right, box_top + NumberInputHeight());
            return false;

        case KEY_OK:
        {
            if (length == 0)
            {
                message = L"Type a number first.";
                break;
            }

            const ULONGLONG value = parse_digits(digits, length, input_base(unit));

            ULONGLONG min_units, max_units;
            if (!unit_range(min, max, unit, &min_units, &max_units))
            {
                message = L"Not a whole number of these, use a smaller unit.";
                break;
            }
            if (value < min_units || value > max_units)
            {
                wchar_t low[24], high[24];
                format_units(min_units, unit, low);
                format_units(max_units, unit, high);
                swprintf(message_text, L"Must be from %s to %s %s.", low, high, g_InputUnitShortNames[unit]);
                message = message_text;
                break;
            }

            // value <= max / unit, so this can't overflow
            *result = value * g_InputUnitSectors[unit];
            m_WaitForRelease = true;
            AnimatePanelClose(m_Layout->dialog_left, box_top, m_Layout->dialog_right, box_top + NumberInputHeight());
            return true;
        }
        }
    }
}

int App::NumberInputHeight()
{
    const int gap = m_Layout->menu_selection_inset;
    const int key_height = m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->dialog_button_padding);
    const int line_height = m_Layout->dialog_line_height;
    return m_Layout->menu_first_item_offset + key_height + gap + 2 * line_height + gap +
           KEYPAD_ROWS * (key_height + gap) - gap + m_Layout->menu_footer_height;
}

void App::DrawNumberInput(LPCWSTR title, const wchar_t *digits, int unit, ULONGLONG current, ULONGLONG min, ULONGLONG max,
                          int selected_key, LPCWSTR message)
{
    const int left = m_Layout->dialog_left;
    const int right = m_Layout->dialog_right;
    const int inset = m_Layout->dialog_text_inset;
    const int gap = m_Layout->menu_selection_inset;
    const int key_height = m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->dialog_button_padding);
    const int key_width = (right - left - 2 * inset - (KEYPAD_COLUMNS - 1) * gap) / KEYPAD_COLUMNS;
    const int line_height = m_Layout->dialog_line_height;

    const int height = NumberInputHeight();
    const int top = (m_Layout->height - height) / 2;
    const int bottom = top + height;
    const int value_y = top + m_Layout->menu_first_item_offset;
    const int now_y = value_y + key_height + gap;
    const int range_y = now_y + line_height;
    const int keys_y = range_y + line_height + gap;

    wchar_t text[100];
    wchar_t amount[40];

    DrawPanel(left, top, right, bottom, title, value_y);

    m_Ui.Rect((FLOAT)(left + inset), (FLOAT)value_y, (FLOAT)(right - inset), (FLOAT)(value_y + key_height),
              Colors().selection, Colors().selection);
    swprintf(text, (input_base(unit) == 16) ? L"0x%s_" : L"%s_", digits);
    m_Ui.Text(FONT_TABLE, text, left + inset + gap, m_Ui.TextYCentered(FONT_TABLE, value_y, value_y + key_height), COLOR_YELLOW);
    m_Ui.Text(FONT_TEXT, g_InputUnitNames[unit], right - inset - gap, m_Ui.TextYCentered(FONT_TEXT, value_y, value_y + key_height),
              COLOR_WHITE, XFONT_RIGHT);

    if (g_InputUnitSectors[unit] == 1)
    {
        format_units(current, unit, amount);
        wcscat(amount, L" sectors");
    }
    else
    {
        swprintf(amount, L"%.2f %s", (double)current / g_InputUnitSectors[unit], g_InputUnitNames[unit]);
    }
    swprintf(text, L"Now: %s", amount);
    m_Ui.Text(FONT_TEXT, text, left + inset, now_y, COLOR_GREY);

    ULONGLONG min_units, max_units;
    if (unit_range(min, max, unit, &min_units, &max_units))
    {
        wchar_t low[24], high[24];
        format_units(min_units, unit, low);
        format_units(max_units, unit, high);
        swprintf(text, L"Can be: %s to %s %s", low, high, g_InputUnitShortNames[unit]);
    }
    else
    {
        swprintf(text, L"Can be: less than one %s, use a smaller unit", g_InputUnitShortNames[unit]);
    }
    m_Ui.Text(FONT_TEXT, text, left + inset, range_y, COLOR_GREY);

    for (int r = 0; r < KEYPAD_ROWS; r++)
    {
        for (int c = 0; c < KEYPAD_COLUMNS; c++)
        {
            const int key = g_KeypadKeys[r][c];
            const int x = left + inset + c * (key_width + gap);
            const int y = keys_y + r * (key_height + gap);
            const bool selected = (key == selected_key);
            const bool usable = (key > 0xF || key < input_base(unit)); // A-F only in hex
            const D3DCOLOR color = selected ? Colors().selection : Colors().button;

            LPCWSTR label = text;
            switch (key)
            {
            case KEY_DELETE: label = L"Delete"; break;
            case KEY_UNIT:   label = L"Unit"; break;
            case KEY_CLEAR:  label = L"Clear"; break;
            case KEY_CANCEL: label = L"Cancel"; break;
            case KEY_OK:     label = L"OK"; break;
            default:         swprintf(text, L"%X", key); break;
            }

            m_Ui.Rect((FLOAT)x, (FLOAT)y, (FLOAT)(x + key_width), (FLOAT)(y + key_height), color, color);
            m_Ui.Text(FONT_TEXT, label, x + key_width / 2, m_Ui.TextYCentered(FONT_TEXT, y, y + key_height),
                      selected ? COLOR_YELLOW : (usable ? COLOR_WHITE : COLOR_GREY), XFONT_CENTER);
        }
    }

    if (message != NULL)
    {
        DrawPanelFooter(left, right, bottom, message, COLOR_RED);
    }
    else
    {
        DrawPanelFooter(left, right, bottom, L"A: press   X: delete   Y: unit   Start: OK   B: cancel");
    }
}

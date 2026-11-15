#include "gamepad_input.h"
#include <string.h>

static const DWORD REPEAT_DELAY_MS = 300; // Before a held D-pad starts repeating
static const DWORD REPEAT_RATE_MS = 120;  // Between repeats
static const int ANALOG_BUTTON_COUNT = 8;

int DpadRepeat::Step(int direction)
{
    if (direction == 0)
    {
        m_held = 0;
        m_repeating = false;
        return 0;
    }

    const DWORD now = GetTickCount();
    const bool new_press = (direction != m_held);
    m_held = direction;

    if (new_press || now - m_last_move >= (m_repeating ? REPEAT_RATE_MS : REPEAT_DELAY_MS))
    {
        m_repeating = !new_press;
        m_last_move = now;
        return direction;
    }

    return 0;
}

GamepadInput::GamepadInput() : m_analog_button(-1), m_digital_buttons(0), m_waiting_for_release(false)
{
    memset(m_pads, 0, sizeof(m_pads));
}

void GamepadInput::OpenPresentPads()
{
    const DWORD present = XGetDevices(XDEVICE_TYPE_GAMEPAD);
    for (int port = 0; port < PORT_COUNT; port++)
    {
        if (present & (1 << port))
        {
            m_pads[port] = XInputOpen(XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, 0);
        }
    }
}

bool GamepadInput::Read()
{
    const bool pressed = ReadFirstPressed();
    if (m_waiting_for_release)
    {
        if (!pressed)
        {
            m_waiting_for_release = false;
        }
        m_analog_button = -1;
        m_digital_buttons = 0;
        return false;
    }
    return true;
}

void GamepadInput::WaitForRelease()
{
    m_waiting_for_release = true;
}

// Pads come and go: removals first, so one replugged between two polls
// is reopened
void GamepadInput::UpdatePads()
{
    DWORD insertions = 0;
    DWORD removals = 0;
    if (!XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &insertions, &removals))
    {
        return;
    }

    for (int port = 0; port < PORT_COUNT; port++)
    {
        const DWORD mask = 1 << port;
        if ((removals & mask) && m_pads[port] != NULL)
        {
            XInputClose(m_pads[port]);
            m_pads[port] = NULL;
        }
        if ((insertions & mask) && m_pads[port] == NULL)
        {
            m_pads[port] = XInputOpen(XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, 0);
        }
    }
}

// Cleared first: a failed read (pad pulled out) leaves stale state. False if
// the pad is gone or the read failed.
bool GamepadInput::ReadPad(int port, XINPUT_STATE *state)
{
    memset(state, 0, sizeof(*state));
    if (m_pads[port] == NULL)
    {
        return false;
    }

    if (XInputGetState(m_pads[port], state) != ERROR_SUCCESS)
    {
        memset(state, 0, sizeof(*state));
        return false;
    }
    return true;
}

// The first pad with something pressed, in port order. False if nothing is.
bool GamepadInput::ReadFirstPressed()
{
    m_analog_button = -1;
    m_digital_buttons = 0;
    UpdatePads();

    for (int port = 0; port < PORT_COUNT; port++)
    {
        XINPUT_STATE state;
        if (ReadPad(port, &state) && TakePressed(state))
        {
            return true;
        }
    }
    return false;
}

// Digital buttons first; analog ones count above XINPUT_GAMEPAD_MAX_CROSSTALK
// (worn pads report small values at rest). False if nothing is pressed.
bool GamepadInput::TakePressed(const XINPUT_STATE &state)
{
    if (state.Gamepad.wButtons)
    {
        m_digital_buttons = state.Gamepad.wButtons;
        return true;
    }

    for (int button = 0; button < ANALOG_BUTTON_COUNT; button++)
    {
        if (state.Gamepad.bAnalogButtons[button] > XINPUT_GAMEPAD_MAX_CROSSTALK)
        {
            m_analog_button = button;
            return true;
        }
    }
    return false;
}

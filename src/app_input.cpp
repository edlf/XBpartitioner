// Controllers and the held D-pad repeat.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

static const DWORD REPEAT_DELAY_MS = 300; // Before a held D-pad starts repeating
static const DWORD REPEAT_RATE_MS = 120;  // Between repeats

int DpadRepeat::Step(int direction)
{
    if (direction == 0)
    {
        held = 0;
        repeating = false;
        return 0;
    }

    const DWORD now = GetTickCount();
    const bool new_press = (direction != held);
    held = direction;

    if (new_press || now - last_move >= (repeating ? REPEAT_RATE_MS : REPEAT_DELAY_MS))
    {
        repeating = !new_press;
        last_move = now;
        return direction;
    }

    return 0;
}

bool App::ReadInput()
{
    const bool pressed = (XInputGetStateAny(&m_AnalogButton, &m_DigitalButtons, 0) != -1);
    if (m_WaitForRelease)
    {
        if (!pressed)
        {
            m_WaitForRelease = false;
        }
        m_AnalogButton = -1;
        m_DigitalButtons = 0;
        return false;
    }
    return true;
}

// Pads come and go: removals first, so one replugged between two polls
// is reopened
void App::UpdateControllers()
{
    DWORD insertions = 0;
    DWORD removals = 0;
    if (!XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &insertions, &removals))
    {
        return;
    }

    for (int i = 0; i < ARRAY_COUNT(m_Controllers); i++)
    {
        const DWORD mask = 1 << i;
        if ((removals & mask) && m_Controllers[i] != NULL)
        {
            XInputClose(m_Controllers[i]);
            m_Controllers[i] = NULL;
        }
        if ((insertions & mask) && m_Controllers[i] == NULL)
        {
            m_Controllers[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, XDEVICE_NO_SLOT, 0);
        }
    }
}

// Cleared first: a failed read (pad pulled out) leaves stale state
bool App::ReadController(int port, XINPUT_STATE *state)
{
    memset(state, 0, sizeof(*state));
    if (m_Controllers[port] == NULL)
    {
        return false;
    }

    if (XInputGetState(m_Controllers[port], state) != ERROR_SUCCESS)
    {
        memset(state, 0, sizeof(*state));
        return false;
    }
    return true;
}

// Reads the first pad with something pressed: *analog the analog button
// (-1 if none), *buttons the digital ones. wait: 0 polls, 1 waits for a
// press, 2 also for its release. Returns the pad, -1 if nothing (wait 0).
// Analog buttons count above XINPUT_GAMEPAD_MAX_CROSSTALK: worn pads
// report small values at rest.
int App::XInputGetStateAny(int *analog, int *buttons, int wait)
{
    XINPUT_STATE ControllerState[4];
    int c = -1;
    *analog = -1;
    *buttons = 0;

    do
    {
        UpdateControllers();

        for (int i = 0; i < 4 && c == -1; i++)
        {
            if (!ReadController(i, &ControllerState[i]))
            {
                continue;
            }

            if (ControllerState[i].Gamepad.wButtons)
            {
                *buttons = ControllerState[i].Gamepad.wButtons;
                c = i;
                break;
            }

            for (int j = 0; j < 8; j++)
            {
                if (ControllerState[i].Gamepad.bAnalogButtons[j] > XINPUT_GAMEPAD_MAX_CROSSTALK)
                {
                    *analog = j;
                    c = i;
                    break;
                }
            }
        }

        if (!wait)
        {
            return c;
        }

        if (c == -1)
        {
            Sleep(10);
        }
    } while (c == -1);

    while (wait == 2)
    {
        // A pad pulled out counts as released, or this would wait forever
        UpdateControllers();
        if (!ReadController(c, &ControllerState[c]))
        {
            break;
        }

        // *analog is -1 when a digital button was pressed
        if (!ControllerState[c].Gamepad.wButtons &&
            (*analog < 0 || ControllerState[c].Gamepad.bAnalogButtons[*analog] <= XINPUT_GAMEPAD_MAX_CROSSTALK))
        {
            break;
        }
        Sleep(10);
    }

    return c;
}

#ifndef GAMEPAD_INPUT_H_
#define GAMEPAD_INPUT_H_

#include <xtl.h>

// Held D-pad repeat, timed so screens keep drawing. Step() runs once per
// frame with the direction held and returns the move: at once, then after
// a delay, repeating.
class DpadRepeat
{
  public:
    DpadRepeat() : m_held(0), m_last_move(0), m_repeating(false)
    {
    }
    int Step(int direction);

  private:
    int m_held;
    DWORD m_last_move;
    bool m_repeating;
};

// The controllers in the four ports, which come and go, and what is pressed
class GamepadInput
{
  public:
    GamepadInput();

    // At startup, once the devices are enumerated; later pads are picked up
    // while reading
    void OpenPresentPads();

    // Reads the first pad with something pressed. False (and nothing pressed)
    // while waiting for a release, see WaitForRelease().
    bool Read();
    // Ignore everything until all buttons are released, so a held button
    // counts once and never carries over to the next screen
    void WaitForRelease();

    // XINPUT_GAMEPAD_A and so on, -1 if none
    int AnalogButton() const
    {
        return m_analog_button;
    }
    // XINPUT_GAMEPAD_DPAD_UP and so on
    int DigitalButtons() const
    {
        return m_digital_buttons;
    }
    bool AnyPressed() const
    {
        return m_analog_button >= 0 || m_digital_buttons != 0;
    }

  private:
    static const int PORT_COUNT = 4;

    void UpdatePads();
    bool ReadPad(int port, XINPUT_STATE *state);
    bool ReadFirstPressed();
    bool TakePressed(const XINPUT_STATE &state);

    HANDLE m_pads[PORT_COUNT];
    int m_analog_button;
    int m_digital_buttons;
    bool m_waiting_for_release;
};

#endif // GAMEPAD_INPUT_H_

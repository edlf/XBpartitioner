#ifndef NUMBER_PAD_H_
#define NUMBER_PAD_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "layout.h"
#include "typed_number.h"

// What the number pad asks for. Values in sectors.
struct NumberInputRequest
{
    LPCWSTR title;
    ULONGLONG current;
    ULONGLONG min;
    ULONGLONG max;
    int unit; // InputUnit it starts in
};

// The on screen number pad for typing a start, size or end, drawn over what
// the dialogs' backdrop shows
class NumberPad
{
  public:
    explicit NumberPad(const ScreenTools &tools);
    // Once the video mode is known
    void Configure(const Layout *layout);

    // D-pad picks a key, A presses it, X deletes, Y changes the unit, Start is
    // OK, B cancels. False if cancelled; *result is in sectors, within min..max.
    bool Run(const NumberInputRequest &request, ULONGLONG *result);

  private:
    enum Outcome
    {
        PAD_OPEN,
        PAD_OK,
        PAD_CANCELLED
    };

    // While the number pad is open
    struct PadState
    {
        explicit PadState(int unit);

        TypedNumber number;
        int row; // The selected key
        int column;
        LPCWSTR message; // An error in the footer, or NULL
        wchar_t message_text[INPUT_TEXT_COUNT];
        ULONGLONG sectors; // The result, once OK
        DpadRepeat vertical_repeat;
        DpadRepeat horizontal_repeat;
    };

    // Where the pad's parts are, in layout space
    struct PadGeometry
    {
        int left;
        int right;
        int inset;
        int gap;
        int key_width;
        int key_height;
        int top;
        int bottom;
        int value_y;
        int now_y;
        int range_y;
        int keys_y;
    };

    Outcome HandleButtons(const NumberInputRequest &request, PadState &pad);
    void MoveCursor(PadState &pad);
    int PressedKey(const PadState &pad) const;
    Outcome PressKey(const NumberInputRequest &request, PadState &pad, int key);
    Outcome PressOk(const NumberInputRequest &request, PadState &pad);

    void Draw(const NumberInputRequest &request, const PadState &pad);
    void DrawValue(const PadState &pad, const PadGeometry &geometry);
    void DrawKeys(const PadState &pad, const PadGeometry &geometry);
    PadGeometry Geometry();
    int Height();
    PanelRect Box();
    const Theme &Colors() const
    {
        return m_ui.GetTheme();
    }

    Renderer &m_ui;
    GamepadInput &m_input;
    DialogPresenter &m_dialogs;
    const Layout *m_layout;
};

#endif // NUMBER_PAD_H_

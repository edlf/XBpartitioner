#ifndef DIALOG_PRESENTER_H_
#define DIALOG_PRESENTER_H_

#include <xtl.h>
#include "gamepad_input.h"
#include "layout.h"
#include "ui.h"

// What DialogPresenter::RunDialog() says
struct DialogText
{
    LPCWSTR title;
    Color title_color;
    const LPCWSTR *lines;
    int line_count;
};

// RunDialog()'s buttons; without a cancel label (NULL) just OK
struct DialogButtons
{
    LPCWSTR ok_label;
    LPCWSTR cancel_label;
};

const DialogButtons DIALOG_OK_ONLY = {L"OK", NULL};

// Slow work for a worker thread (DialogPresenter::RunWithBusyWindow)
struct BusyWork
{
    LPTHREAD_START_ROUTINE routine;
    LPVOID parameter;
    volatile LONG *percent; // Progress the routine updates, or NULL
};

const size_t INFO_ROW_VALUE_COUNT = 80;

struct InfoRow
{
    LPCWSTR label;
    wchar_t value[INFO_ROW_VALUE_COUNT];
    Color color;
};

// A panel's box on screen, in layout space
struct PanelRect
{
    int left;
    int top;
    int right;
    int bottom;
};

enum PanelAnimation
{
    PANEL_OPENING,
    PANEL_CLOSING
};

// What is on screen behind a panel, drawn again every frame so it keeps moving
class ScreenBackdrop
{
  public:
    virtual ~ScreenBackdrop()
    {
    }
    // Inside BeginFrame()/EndFrame()
    virtual void DrawBackdrop() = 0;
};

// Dialogs, messages, the busy window, info panels, and the panels they are
// drawn in: open, draw, read the pad, close. In layout space (see ui.h).
class DialogPresenter
{
  public:
    DialogPresenter(Renderer &ui, GamepadInput &input, ScreenBackdrop &backdrop);
    // Once the video mode and the settings are known
    void Configure(const Layout *layout, bool animations);

    // With a cancel label: OK and Cancel (Cancel selected), true for OK.
    // Without: just OK, any button closes it and it returns true.
    bool RunDialog(const DialogText &text, const DialogButtons &buttons);
    // Cancel selected to start with. True for OK.
    bool ConfirmWarning(const LPCWSTR *lines, int line_count, LPCWSTR ok_label);
    void ShowWarning(const LPCWSTR *lines, int line_count);
    void ShowError(const LPCWSTR *lines, int line_count);
    void ShowMessage(LPCWSTR title, Color title_color, LPCWSTR message);
    void ShowInfoPanel(LPCWSTR title, const InfoRow *rows, int row_count);

    // A one line window ("Saving settings...") around slow work: open it, do
    // the work, close it with what OpenBusyWindow returned
    DWORD OpenBusyWindow(LPCWSTR message);
    void CloseBusyWindow(LPCWSTR message, DWORD opened);
    // Runs slow work on a worker thread behind the busy window, with its
    // progress after the message if it has one
    void RunWithBusyWindow(const BusyWork &work, LPCWSTR message);

    // For other screens' panels: a frame with what is behind them, then the
    // panel's background, border, title and a line under it
    void BeginPanelFrame();
    void DrawPanel(const PanelRect &box, LPCWSTR title, int first_item_y);
    void DrawPanelFooter(const PanelRect &box, LPCWSTR text, Color color = COLOR_GREY);
    // Opening grows the empty panel from a line across its middle, closing
    // shrinks it back. Off with animations=0.
    void AnimatePanelOpen(const PanelRect &box);
    void AnimatePanelClose(const PanelRect &box);

    // A menu item's selection bar, and the pitch between items
    int MenuItemBarHeight() const;
    int MenuItemHeight() const;

  private:
    enum DialogAnswer
    {
        DIALOG_OPEN,
        DIALOG_OK,
        DIALOG_CANCEL
    };

    // Where a dialog is, and which button is selected
    struct DialogView
    {
        PanelRect box;
        int buttons_y;
        int button_height;
        int ok_x;
        int cancel_x;
        bool ok_selected;
    };

    DialogView PlaceDialog(const DialogText &text, const DialogButtons &buttons) const;
    void DrawDialog(const DialogText &text, const DialogButtons &buttons, const DialogView &view);
    void DrawDialogButtons(const DialogButtons &buttons, const DialogView &view);
    DialogAnswer HandleDialogButtons(const DialogButtons &buttons, DialogView &view);

    void AnimatePanel(const PanelRect &box, PanelAnimation animation);
    PanelRect BusyWindowBox();
    void DrawBusyWindow(LPCWSTR message);
    const Theme &Colors() const
    {
        return m_ui.GetTheme();
    }

    Renderer &m_ui;
    GamepadInput &m_input;
    ScreenBackdrop &m_backdrop;
    const Layout *m_layout;
    bool m_animations;
};

// What a screen of its own draws and reads with: they must outlive it
struct ScreenTools
{
    Renderer &ui;
    GamepadInput &input;
    DialogPresenter &dialogs;
};

#endif // DIALOG_PRESENTER_H_

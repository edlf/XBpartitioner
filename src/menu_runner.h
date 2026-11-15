#ifndef MENU_RUNNER_H_
#define MENU_RUNNER_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "gamepad_input.h"
#include "layout.h"
#include "ui.h"

// MenuRunner::Run() results besides the selected item
const int MENU_RESULT_BACK = -1;  // B: back one level
const int MENU_RESULT_CLOSE = -2; // Start: close all menus

// Menu footers: B closes the only menu, or goes back one level
const wchar_t MENU_FOOTER_TOP_LEVEL[] = L"A: select    B: close";
const wchar_t MENU_FOOTER_SUBMENU[] = L"A: select    B: back    Start: close";

// Shows what the selected item would do while the menu is open (a theme)
class MenuPreview
{
  public:
    virtual ~MenuPreview()
    {
    }
    // With the selected item at the start and whenever it changes
    virtual void Preview(int item) = 0;
};

// What a menu shows
struct MenuRequest
{
    LPCWSTR title;
    const LPCWSTR *labels;
    const bool *enabled; // Disabled items are skipped
    int count;
    int initial;    // Selected at the start
    LPCWSTR footer; // MENU_FOOTER_TOP_LEVEL or MENU_FOOTER_SUBMENU
    MenuPreview *preview; // Or NULL
};

// A list in a panel: up/down moves (skipping disabled items, scrolling past
// eight), A selects, B goes back, Start closes every menu
class MenuRunner
{
  public:
    MenuRunner(Renderer &ui, GamepadInput &input, DialogPresenter &dialogs);
    // Once the video mode is known
    void Configure(const Layout *layout);

    // The selected item, MENU_RESULT_BACK or MENU_RESULT_CLOSE. Cancelled with
    // a preview: the item selected at the start is previewed again first.
    int Run(const MenuRequest &menu);

  private:
    // Where the menu's parts are, in layout space
    struct MenuGeometry
    {
        PanelRect box;
        int first_item_y;
        int bar_height;
        int item_height;
        int visible; // Items shown at once; more scroll
        bool scrolls;
        FLOAT selection_right; // The bars stop short of the scroll bar
        FLOAT scroll_left;
        FLOAT scroll_right;
        int track_top;
        int track_height;
        int thumb_height;
    };

    // While the menu is open
    struct MenuState
    {
        int selected;
        int start_item; // What a preview shows again when the menu is cancelled
        int previewed;  // -1: nothing yet
        int first_visible;
        FLOAT bar_y;
        FLOAT thumb_y;
        DWORD slide_tick;
        DWORD thumb_tick;
        DpadRepeat repeat;
    };

    MenuGeometry Geometry(int count) const;
    // MENU_STILL_OPEN until an item is chosen or the menu is left
    int Step(const MenuRequest &menu, const MenuGeometry &geometry, MenuState &state);
    void Animate(const MenuRequest &menu, const MenuGeometry &geometry, MenuState &state);
    // Top of the scroll bar's thumb for the first item shown
    static FLOAT ThumbTop(const MenuGeometry &geometry, int count, int first_visible);
    void Draw(const MenuRequest &menu, const MenuGeometry &geometry, const MenuState &state);
    void DrawScrollBar(const MenuGeometry &geometry, const MenuState &state);
    void DrawItems(const MenuRequest &menu, const MenuGeometry &geometry, const MenuState &state);
    int HandleButtons(const MenuRequest &menu, MenuState &state);
    // Cancelled with a preview: the start item is previewed again, so the menu
    // closes as it was (e.g. the original theme)
    void RestorePreview(const MenuRequest &menu, const MenuState &state);

    const Theme &Colors() const
    {
        return m_ui.GetTheme();
    }

    Renderer &m_ui;
    GamepadInput &m_input;
    DialogPresenter &m_dialogs;
    const Layout *m_layout;
};

#endif // MENU_RUNNER_H_

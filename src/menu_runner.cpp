#include "stdafx.h"
#include "menu_runner.h"
#include "selection_slide.h"

// More items scroll, with a scroll bar
static const int MENU_MAX_VISIBLE_ITEMS = 8;

// MenuRunner::Step() while no item is chosen and the menu isn't left
static const int MENU_STILL_OPEN = -3;

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

// The first enabled item from initial on, or -1 if none is
static int first_enabled_item(const MenuRequest &menu)
{
    int selected = (menu.initial >= 0 && menu.initial < menu.count) ? menu.initial : 0;
    for (int tries = 0; tries < menu.count && !menu.enabled[selected]; tries++)
    {
        selected = (selected + 1) % menu.count;
    }
    return menu.enabled[selected] ? selected : -1;
}

MenuRunner::MenuRunner(Renderer &ui, GamepadInput &input, DialogPresenter &dialogs)
    : m_ui(ui), m_input(input), m_dialogs(dialogs), m_layout(&g_Layout480)
{
}

void MenuRunner::Configure(const Layout *layout)
{
    m_layout = layout;
}

int MenuRunner::Run(const MenuRequest &menu)
{
    const int start_item = first_enabled_item(menu);
    if (start_item < 0)
    {
        return MENU_RESULT_BACK;
    }

    const MenuGeometry geometry = Geometry(menu.count);
    MenuState state;
    state.selected = start_item;
    state.start_item = start_item;
    state.previewed = -1;
    state.first_visible = menu_scroll_to(start_item, 0, geometry.visible);
    state.bar_y = (FLOAT)(geometry.first_item_y + (start_item - state.first_visible) * geometry.item_height);
    state.thumb_y = ThumbTop(geometry, menu.count, state.first_visible);
    state.slide_tick = 0;
    state.thumb_tick = 0;

    m_input.WaitForRelease();
    m_dialogs.AnimatePanelOpen(geometry.box);

    int result = MENU_STILL_OPEN;
    while (result == MENU_STILL_OPEN)
    {
        result = Step(menu, geometry, state);
    }

    // Every way out closes the box
    m_input.WaitForRelease();
    if (result < 0)
    {
        RestorePreview(menu, state);
    }
    m_dialogs.AnimatePanelClose(geometry.box);
    return result;
}

MenuRunner::MenuGeometry MenuRunner::Geometry(int count) const
{
    MenuGeometry geometry;
    geometry.bar_height = m_dialogs.MenuItemBarHeight();
    geometry.item_height = m_dialogs.MenuItemHeight();
    geometry.visible = (count > MENU_MAX_VISIBLE_ITEMS) ? MENU_MAX_VISIBLE_ITEMS : count;
    geometry.scrolls = (geometry.visible < count);

    const int top = m_layout->menu_top;
    const int height = m_layout->menu_first_item_offset + geometry.visible * geometry.item_height + m_layout->menu_footer_height;
    const PanelRect box = {m_layout->menu_left, top, m_layout->menu_right, top + height};
    geometry.box = box;
    geometry.first_item_y = top + m_layout->menu_first_item_offset;

    const int scroll_width = (m_layout->menu_selection_inset + 1) / 2;
    geometry.scroll_right = (FLOAT)(box.right - m_layout->menu_selection_inset);
    geometry.scroll_left = geometry.scroll_right - scroll_width;
    geometry.selection_right = geometry.scrolls ? geometry.scroll_left - m_layout->menu_selection_inset / 2 : geometry.scroll_right;
    geometry.track_top = geometry.first_item_y;
    geometry.track_height = geometry.visible * geometry.item_height - m_layout->menu_item_gap;
    geometry.thumb_height = geometry.scrolls ? (geometry.track_height * geometry.visible + count / 2) / count : geometry.track_height;
    return geometry;
}

int MenuRunner::Step(const MenuRequest &menu, const MenuGeometry &geometry, MenuState &state)
{
    if (menu.preview != NULL && state.previewed != state.selected)
    {
        menu.preview->Preview(state.selected);
        state.previewed = state.selected;
    }

    Animate(menu, geometry, state);
    Draw(menu, geometry, state);

    if (!m_input.Read())
    {
        state.repeat.Step(0);
        return MENU_STILL_OPEN;
    }
    return HandleButtons(menu, state);
}

// Scrolls the selected item into view; the bar and the scroll bar's thumb slide
void MenuRunner::Animate(const MenuRequest &menu, const MenuGeometry &geometry, MenuState &state)
{
    state.first_visible = menu_scroll_to(state.selected, state.first_visible, geometry.visible);
    SlideTowards(state.bar_y, (FLOAT)(geometry.first_item_y + (state.selected - state.first_visible) * geometry.item_height), state.slide_tick);
    SlideTowards(state.thumb_y, ThumbTop(geometry, menu.count, state.first_visible), state.thumb_tick);
}

// Top of the scroll bar's thumb for the first item shown
FLOAT MenuRunner::ThumbTop(const MenuGeometry &geometry, int count, int first_visible)
{
    if (count <= geometry.visible)
    {
        return (FLOAT)geometry.track_top;
    }
    const int free_track = geometry.track_height - geometry.thumb_height;
    return (FLOAT)geometry.track_top + (FLOAT)(free_track * first_visible) / (FLOAT)(count - geometry.visible);
}

void MenuRunner::Draw(const MenuRequest &menu, const MenuGeometry &geometry, const MenuState &state)
{
    m_dialogs.BeginPanelFrame();
    m_dialogs.DrawPanel(geometry.box, menu.title, geometry.first_item_y);

    // Before the text, so it never covers an item while moving
    const int selection_top = (int)(state.bar_y + 0.5f);
    m_ui.Rect((FLOAT)(geometry.box.left + m_layout->menu_selection_inset),
              (FLOAT)selection_top,
              geometry.selection_right,
              (FLOAT)(selection_top + geometry.bar_height),
              Colors().selection,
              Colors().selection);

    if (geometry.scrolls)
    {
        DrawScrollBar(geometry, state);
    }
    DrawItems(menu, geometry, state);

    m_dialogs.DrawPanelFooter(geometry.box, menu.footer);
    m_ui.EndFrame();
}

void MenuRunner::DrawScrollBar(const MenuGeometry &geometry, const MenuState &state)
{
    const FLOAT left = geometry.scroll_left;
    const FLOAT right = geometry.scroll_right;
    const int thumb_top = (int)(state.thumb_y + 0.5f);
    m_ui.Rect(left, (FLOAT)geometry.track_top, right, (FLOAT)(geometry.track_top + geometry.track_height), Colors().button, Colors().button);
    m_ui.Rect(left, (FLOAT)thumb_top, right, (FLOAT)(thumb_top + geometry.thumb_height), Colors().panel_border, Colors().panel_border);
}

void MenuRunner::DrawItems(const MenuRequest &menu, const MenuGeometry &geometry, const MenuState &state)
{
    for (int i = state.first_visible; i < state.first_visible + geometry.visible; i++)
    {
        const int bar_top = geometry.first_item_y + (i - state.first_visible) * geometry.item_height;
        const int bar_bottom = bar_top + geometry.bar_height;
        Color color = menu.enabled[i] ? COLOR_WHITE : COLOR_GREY;
        if (i == state.selected)
        {
            color = COLOR_YELLOW;
        }
        m_ui.Text(FONT_TEXT, menu.labels[i], m_layout->menu_left + m_layout->menu_text_inset, m_ui.TextYCentered(FONT_TEXT, bar_top, bar_bottom), color);
    }
}

// Up/down moves, skipping disabled items; A selects, B goes back, Start closes
int MenuRunner::HandleButtons(const MenuRequest &menu, MenuState &state)
{
    const int buttons = m_input.DigitalButtons();
    const int analog = m_input.AnalogButton();
    const int direction = (buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
    const int move = state.repeat.Step(direction);
    if (move != 0)
    {
        // At least one item is enabled, so this ends
        const int step = (move < 0) ? menu.count - 1 : 1;
        do
        {
            state.selected = (state.selected + step) % menu.count;
        } while (!menu.enabled[state.selected]);
        return MENU_STILL_OPEN;
    }

    if (buttons & XINPUT_GAMEPAD_START)
    {
        return MENU_RESULT_CLOSE;
    }
    if (analog == XINPUT_GAMEPAD_B)
    {
        return MENU_RESULT_BACK;
    }
    if (analog == XINPUT_GAMEPAD_A && menu.enabled[state.selected])
    {
        return state.selected;
    }
    return MENU_STILL_OPEN;
}

void MenuRunner::RestorePreview(const MenuRequest &menu, const MenuState &state)
{
    if (menu.preview != NULL && state.previewed != state.start_item)
    {
        menu.preview->Preview(state.start_item);
    }
}

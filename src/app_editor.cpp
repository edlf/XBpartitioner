// The partition editor page.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// Top to bottom
enum EditField
{
    EDIT_ENABLED = 0,
    EDIT_START,
    EDIT_SIZE,
    EDIT_END,
    EDIT_STEP,
    EDIT_FILL,
    EDIT_BACK,
    EDIT_FIELD_COUNT
};

// In sectors
static const ULONG g_EditSteps[] = {0x8, 0x800, 0x40000, 0x200000, 0x1400000, 0xC800000};

static const LPCWSTR g_EditStepNames[] = {L"4 KB", L"1 MB", L"128 MB", L"1 GB", L"10 GB", L"100 GB"};

static const int EDIT_STEP_COUNT = ARRAY_COUNT(g_EditSteps);

static const int EDIT_DEFAULT_STEP = 3; // 1 GB

// Laid out like the info panel: fields, a divider, then read only details
static const int EDIT_DETAIL_COUNT = 5; // Name, format, on disk, free space, status

int App::EditPanelHeight() const
{
    return m_Layout->menu_first_item_offset + (EDIT_FIELD_COUNT + EDIT_DETAIL_COUNT) * MenuItemHeight() +
           MenuItemHeight() / 2 + m_Layout->menu_footer_height;
}

int App::EditPanelTop() const
{
    return (m_Layout->height - EditPanelHeight()) / 2;
}

// Edits the selected partition in the edited table (nothing is written).
// A on start, size or end types a value, Y resets to the written table.
void App::DisplayEditPartition()
{
    const int part = m_CurrentPart;
    int field = EDIT_SIZE;
    int step = EDIT_DEFAULT_STEP;
    LPCWSTR message = NULL; // One-off feedback in the footer
    Color message_color = COLOR_RED;

    const int first_item_y = EditPanelTop() + m_Layout->menu_first_item_offset;
    FLOAT bar_y = (FLOAT)(first_item_y + field * MenuItemHeight());
    DWORD slide_tick = 0;

    AnimatePanelOpen(m_Layout->edit_left, EditPanelTop(), m_Layout->edit_right, EditPanelTop() + EditPanelHeight());
    DpadRepeat field_repeat;
    DpadRepeat change_repeat;

    m_WaitForRelease = true;

    while (true)
    {
        const bool enabled = m_pTable->isPartitionEnabled(part);

        bool field_available[EDIT_FIELD_COUNT];
        for (int i = 0; i < EDIT_FIELD_COUNT; i++)
        {
            field_available[i] = true;
        }
        field_available[EDIT_START] = enabled;
        field_available[EDIT_SIZE] = enabled;
        field_available[EDIT_END] = enabled;
        field_available[EDIT_FILL] = enabled;
        if (!field_available[field])
        {
            field = EDIT_ENABLED;
        }

        SlideTowards(bar_y, (FLOAT)(first_item_y + field * MenuItemHeight()), slide_tick);
        const EditView view = {part, field, field_available, g_EditStepNames[step], message, message_color, bar_y};
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawEditPanel(view);
        m_Ui.EndFrame();

        if (!ReadInput())
        {
            continue;
        }
        const int buttons = m_DigitalButtons;
        const int analog = m_AnalogButton;

        const int vertical = (buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
        const int field_move = field_repeat.Step(vertical);
        if (vertical != 0)
        {
            if (field_move != 0)
            {
                // Enabled is always available, so this ends
                const int move = (field_move < 0) ? EDIT_FIELD_COUNT - 1 : 1;
                do
                {
                    field = (field + move) % EDIT_FIELD_COUNT;
                } while (!field_available[field]);
                message = NULL;
            }
            continue;
        }

        if ((buttons & XINPUT_GAMEPAD_START) || analog == XINPUT_GAMEPAD_B)
        {
            LeaveEditPartition(part);
            return;
        }

        if (analog == XINPUT_GAMEPAD_Y)
        {
            if (m_pTable->RestorePartitionEntry(part, m_pTableOriginal->getPartitionEntry(part)))
            {
                message = L"Partition reset to the saved table.";
                message_color = COLOR_GREEN;
            }
            else
            {
                message = L"Can't reset: other partitions now use that space.";
                message_color = COLOR_RED;
            }
            m_WaitForRelease = true;
            continue;
        }

        const int horizontal = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : (buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : 0;
        const int change = change_repeat.Step(horizontal);
        const bool apply = (analog == XINPUT_GAMEPAD_A);

        if (horizontal == 0 && !apply)
        {
            continue;
        }

        const ULONG step_sectors = g_EditSteps[step];

        if (apply && (field == EDIT_START || field == EDIT_SIZE || field == EDIT_END))
        {
            // The keypad draws this screen behind it
            m_pActiveEditView = &view;
            TypeEditValue(part, field, message, message_color);
            m_pActiveEditView = NULL;
            continue;
        }

        switch (field)
        {
        case EDIT_ENABLED:
            message = NULL;
            if (enabled)
            {
                m_pTable->DisablePartition(part);
            }
            else
            {
                m_pTable->EnablePartition(part);
                if (!m_pTable->isPartitionEnabled(part))
                {
                    message = L"No free space for this partition.";
                    message_color = COLOR_RED;
                }
            }
            m_WaitForRelease = true;
            break;

        case EDIT_START:
            // The end stays: forward shrinks, back grows. change is 0 between repeats.
            if (change > 0)
            {
                message = NULL;
                m_pTable->PartitionStartForward(part, step_sectors);
            }
            else if (change < 0)
            {
                message = NULL;
                m_pTable->PartitionStartBack(part, step_sectors);
            }
            break;

        case EDIT_SIZE:
        case EDIT_END: // Same as the size: the start stays put
            if (change > 0)
            {
                message = NULL;
                m_pTable->PartitionIncreaseSizeBy(part, step_sectors);
            }
            else if (change < 0)
            {
                message = NULL;
                m_pTable->PartitionDecreaseSizeBy(part, step_sectors);
            }
            break;

        case EDIT_STEP:
            // A or Right: bigger steps, Left: smaller
            message = NULL;
            step = (step + (horizontal < 0 ? EDIT_STEP_COUNT - 1 : 1)) % EDIT_STEP_COUNT;
            m_WaitForRelease = true;
            break;

        case EDIT_FILL:
            if (apply)
            {
                // As far as it can (the manipulator clamps)
                message = NULL;
                m_pTable->PartitionIncreaseSizeBy(part, (uint64_t)-1);
            }
            m_WaitForRelease = true;
            break;

        case EDIT_BACK:
            if (apply)
            {
                LeaveEditPartition(part);
                return;
            }
            break;
        }
    }
}

// Types a value with the number pad. Goes through the same functions as
// Left/Right, so it is aligned the same way.
void App::TypeEditValue(int part, int field, LPCWSTR &message, Color &message_color)
{
    const ULONGLONG start = m_pTable->GetPartitionStart(part);
    const ULONGLONG size = m_pTable->GetPartitionSize(part);
    const ULONGLONG end = start + size;
    const ULONGLONG free_before = m_pTable->GetAvailableFreeSpaceForPartitionBefore(part);
    const ULONGLONG free_after = m_pTable->GetAvailableFreeSpaceForPartition(part);
    // Also limited by the maximum size (1TB on v3)
    const ULONGLONG grow_before = m_pTable->GetGrowthLimitBefore(part);
    const ULONGLONG grow_after = m_pTable->GetGrowthLimit(part);

    LPCWSTR title;
    ULONGLONG current, min, max;
    int unit;

    switch (field)
    {
    case EDIT_START:
        // Up to one 4 KB block before the end. A zero size partition moves instead.
        title = L"Start sector";
        current = start;
        min = start - ((size == 0) ? free_before : grow_before);
        if (size == 0)
        {
            max = start + free_after;
        }
        else if (size > LBA_ALIGNMENT)
        {
            max = m_pTable->AlignTo4k(end - LBA_ALIGNMENT);
        }
        else
        {
            max = start;
        }
        unit = INPUT_UNIT_SECTORS_HEX; // Hex, like the editor and the table
        break;

    case EDIT_SIZE:
        title = L"Size";
        current = size;
        min = 0;
        max = size + grow_after;
        unit = INPUT_UNIT_GB;
        break;

    default: // EDIT_END
        title = L"End sector";
        current = end;
        min = start;
        max = end + grow_after;
        unit = INPUT_UNIT_SECTORS_HEX; // Hex, like the editor and the table
        break;
    }

    ULONGLONG value;
    if (!RunNumberInput(title, current, min, max, unit, &value))
    {
        return;
    }

    ULONGLONG result;
    bool end_moved = false;
    switch (field)
    {
    case EDIT_START:
        m_pTable->MovePartitionStartTo(part, value);
        result = m_pTable->GetPartitionStart(part);
        // Unless the end wasn't 4K aligned, or an empty partition moved
        end_moved = (size != 0 && result + m_pTable->GetPartitionSize(part) != end);
        break;
    case EDIT_SIZE:
        m_pTable->ResizePartitionTo(part, value);
        result = m_pTable->GetPartitionSize(part);
        break;
    default:
        m_pTable->ResizePartitionTo(part, value - start);
        result = start + m_pTable->GetPartitionSize(part);
        break;
    }

    // Only the alignment can change it (RunNumberInput checked the range)
    if (end_moved)
    {
        message = L"Start and end rounded to 4 KB boundaries.";
    }
    else
    {
        message = (result != value) ? L"Rounded to a 4 KB boundary." : NULL;
    }
    message_color = COLOR_YELLOW;
}

// A partition left empty is disabled: it would only block its spot
void App::LeaveEditPartition(int part)
{
    if (m_pTable->isPartitionEnabled(part) && m_pTable->GetPartitionSize(part) == 0)
    {
        m_pTable->DisablePartition(part);
    }

    m_WaitForRelease = true;

    AnimatePanelClose(m_Layout->edit_left, EditPanelTop(), m_Layout->edit_right, EditPanelTop() + EditPanelHeight());
}

void App::DrawEditPanel(const EditView &view)
{
    const int part = view.part;
    const int field = view.field;
    const bool *field_available = view.field_available;
    const LPCWSTR step_name = view.step_name;
    const LPCWSTR message = view.message;
    const Color message_color = view.message_color;
    const FLOAT bar_y = view.bar_y;

    const int left = m_Layout->edit_left;
    const int right = m_Layout->edit_right;
    const int top = EditPanelTop();
    const int bar_height = MenuItemBarHeight();
    const int item_height = MenuItemHeight();
    const int first_item_y = top + m_Layout->menu_first_item_offset;
    const int divider_y = first_item_y + EDIT_FIELD_COUNT * item_height + item_height / 4;
    const int first_detail_y = first_item_y + EDIT_FIELD_COUNT * item_height + item_height / 2;
    const int bottom = first_detail_y + EDIT_DETAIL_COUNT * item_height + m_Layout->menu_footer_height;
    const int label_x = left + m_Layout->menu_text_inset;
    const int value_x = left + m_Layout->info_value_offset;

    const ULONGLONG start = m_pTable->GetPartitionStart(part);
    const ULONGLONG size = m_pTable->GetPartitionSize(part);
    const bool enabled = m_pTable->isPartitionEnabled(part);

    wchar_t text[120];
    wchar_t human[32];
    wchar_t lba[16];

    swprintf(text, L"Edit partition %d (%c)", part + 1, partition_letter(part));
    DrawPanel(left, top, right, bottom, text, first_item_y);

    // Before the text, so it never covers it
    const int selection_top = (int)(bar_y + 0.5f);
    m_Ui.Rect((FLOAT)(left + m_Layout->menu_selection_inset), (FLOAT)selection_top,
              (FLOAT)(right - m_Layout->menu_selection_inset), (FLOAT)(selection_top + bar_height),
              Colors().selection, Colors().selection);

    for (int i = 0; i < EDIT_FIELD_COUNT; i++)
    {
        const int bar_top = first_item_y + i * item_height;
        LPCWSTR label = L"";
        text[0] = L'\0';

        switch (i)
        {
        case EDIT_ENABLED:
            label = L"Enabled";
            swprintf(text, enabled ? L"yes" : L"no");
            break;
        case EDIT_START:
            label = L"Start sector";
            format_human_size(start, human);
            format_table_lba(start, lba);
            swprintf(text, L"%s (at %s)", lba, human);
            break;
        case EDIT_SIZE:
            label = L"Size";
            format_human_size(size, human);
            swprintf(text, L"%s (%I64u sectors)", human, size);
            break;
        case EDIT_END:
            label = L"End sector";
            format_human_size(start + size, human);
            format_table_lba(start + size, lba);
            swprintf(text, L"%s (at %s)", lba, human);
            break;
        case EDIT_STEP:
            label = L"Step";
            swprintf(text, L"< %s >", step_name);
            break;
        case EDIT_FILL:
            label = L"Fill free space";
            // What filling adds: the free space, up to the maximum size
            format_human_size(m_pTable->GetGrowthLimit(part), human);
            swprintf(text, L"+%s", human);
            break;
        case EDIT_BACK:
            label = L"Back to main screen";
            break;
        }

        Color color = field_available[i] ? COLOR_WHITE : COLOR_GREY;
        if (i == field)
        {
            color = COLOR_YELLOW;
        }

        // Both fonts have the same height, so they share a baseline
        m_Ui.Text(FONT_TEXT, label, label_x, m_Ui.TextYCentered(FONT_TEXT, bar_top, bar_top + bar_height), color);
        m_Ui.Text(FONT_TABLE, text, value_x, m_Ui.TextYCentered(FONT_TABLE, bar_top, bar_top + bar_height), color);
    }

    m_Ui.Rect((FLOAT)(left + m_Layout->menu_selection_inset), (FLOAT)divider_y,
              (FLOAT)(right - m_Layout->menu_selection_inset), (FLOAT)(divider_y + 1),
              Colors().panel_line, Colors().panel_line);

    struct DetailRow
    {
        LPCWSTR label;
        wchar_t value[80];
        Font font;
        Color color;
    };
    DetailRow details[EDIT_DETAIL_COUNT];

    // 16 single byte characters, space padded
    const XboxPartitionTableEntry entry = m_pTable->getPartitionEntry(part);
    details[0].label = L"Name";
    for (int c = 0; c < (int)XboxPartitionNameMaxLength; c++)
    {
        details[0].value[c] = (entry.Name[c] >= 0x20 && entry.Name[c] < 0x7F) ? (wchar_t)entry.Name[c] : L' ';
    }
    details[0].value[XboxPartitionNameMaxLength] = L'\0';
    details[0].font = FONT_TABLE;
    details[0].color = COLOR_WHITE;

    details[1].label = L"Format uses";
    swprintf(details[1].value, L"%u KB clusters", m_pTable->CalculateClusterSizeForPartition(part));
    details[1].font = FONT_TEXT;
    details[1].color = COLOR_WHITE;

    details[2].label = L"On disk now";
    if (m_ClusterSizes[part])
    {
        swprintf(details[2].value, L"FATX, %u KB clusters", m_ClusterSizes[part]);
    }
    else
    {
        swprintf(details[2].value, L"not formatted (no FATX)");
    }
    details[2].font = FONT_TEXT;
    details[2].color = COLOR_WHITE;

    details[3].label = L"Free space";
    wchar_t before[32];
    wchar_t after[32];
    format_human_size(enabled ? m_pTable->GetAvailableFreeSpaceForPartitionBefore(part) : 0, before);
    format_human_size(enabled ? m_pTable->GetAvailableFreeSpaceForPartition(part) : 0, after);
    swprintf(details[3].value, L"%s before, %s after", before, after);
    details[3].font = FONT_TEXT;
    details[3].color = COLOR_WHITE;

    const bool was_enabled = m_pTableOriginal->isPartitionEnabled(part);
    const bool moved = start != m_pTableOriginal->GetPartitionStart(part) || size != m_pTableOriginal->GetPartitionSize(part);
    details[4].label = L"Status";
    details[4].font = FONT_TEXT;
    details[4].color = COLOR_YELLOW;
    if (!enabled && !was_enabled)
    {
        swprintf(details[4].value, L"disabled");
        details[4].color = COLOR_GREY;
    }
    else if (enabled && !was_enabled)
    {
        swprintf(details[4].value, L"new, format it with write and format");
    }
    else if (!enabled)
    {
        swprintf(details[4].value, L"will be removed when written");
    }
    else if (moved)
    {
        // With the same start, some sizes keep the data. Free space and errors
        // are checked when writing.
        const ULONGLONG old_size = m_pTableOriginal->GetPartitionSize(part);
        const uint32_t cluster_bytes = m_ClusterSizes[part] * 1024;
        const bool formatted = start == m_pTableOriginal->GetPartitionStart(part) && cluster_bytes != 0;
        const FatxResizeKind kind = formatted ? FatxClassifyResize(old_size, cluster_bytes, size) : FATX_RESIZE_KIND_FORMAT;

        if (kind == FATX_RESIZE_KIND_IN_PLACE)
        {
            swprintf(details[4].value, size > old_size ? L"resized, keeps its data" : L"resized, keeps its data if its end is free");
            details[4].color = COLOR_GREEN;
        }
        else if (kind == FATX_RESIZE_KIND_MOVES)
        {
            swprintf(details[4].value, L"resized, keeps its data (clusters move)");
            details[4].color = COLOR_GREEN;
        }
        else if (formatted)
        {
            // Suggest the nearest sizes that work
            const ULONGLONG below = FatxMoveResizeTarget(old_size, cluster_bytes, size);
            const ULONGLONG above = FatxMoveResizeNextUp(old_size, cluster_bytes, size);
            wchar_t below_size[32];
            wchar_t above_size[32];
            format_human_size(below, below_size);
            format_human_size(above, above_size);
            if (below != 0 && above != 0)
            {
                swprintf(details[4].value, L"format needed (data kept: %s or %s)", below_size, above_size);
            }
            else if (below != 0 || above != 0)
            {
                swprintf(details[4].value, L"format needed (data kept: %s)", below != 0 ? below_size : above_size);
            }
            else
            {
                swprintf(details[4].value, L"changed, unreadable until formatted");
            }
        }
        else
        {
            swprintf(details[4].value, L"changed, unreadable until formatted");
        }
    }
    else
    {
        swprintf(details[4].value, L"unchanged");
        details[4].color = COLOR_GREEN;
    }

    for (int d = 0; d < EDIT_DETAIL_COUNT; d++)
    {
        const int row_top = first_detail_y + d * item_height;
        m_Ui.Text(FONT_TEXT, details[d].label, label_x, m_Ui.TextYCentered(FONT_TEXT, row_top, row_top + bar_height), COLOR_GREY);
        m_Ui.Text(details[d].font, details[d].value, value_x, m_Ui.TextYCentered(details[d].font, row_top, row_top + bar_height), details[d].color);
    }

    if (message != NULL)
    {
        DrawPanelFooter(left, right, bottom, message, message_color);
    }
    else
    {
        const bool typeable = (field == EDIT_START || field == EDIT_SIZE || field == EDIT_END);
        DrawPanelFooter(left, right, bottom, typeable ? L"Left/Right: change   A: type it in   Y: reset   B: back"
                                                      : L"Left/Right: change   A: apply   Y: reset   B: back");
    }

}

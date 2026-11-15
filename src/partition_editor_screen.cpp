#include "stdafx.h"
#include "partition_editor_screen.h"
#include "array_length.h"
#include "partition_format.h"
#include "partition_status.h"
#include "selection_slide.h"
#include <stdio.h>
#include <string.h>

// In sectors
static const ULONG g_EditSteps[] = {0x8, 0x800, 0x40000, 0x200000, 0x1400000, 0xC800000};

static const LPCWSTR g_EditStepNames[] = {L"4 KB", L"1 MB", L"128 MB", L"1 GB", L"10 GB", L"100 GB"};

static const int EDIT_STEP_COUNT = ArrayLength(g_EditSteps);

static const int EDIT_DEFAULT_STEP = 3; // 1 GB

// Indexed by EditField
static const LPCWSTR g_FieldLabels[EDIT_FIELD_COUNT] = {
    L"Enabled", L"Start sector", L"Size", L"End sector", L"Step", L"Fill free space", L"Back to main screen",
};

// Laid out like the info panel: fields, a divider, then read only details
static const int EDIT_DETAIL_COUNT = 5; // Name, format, on disk, free space, status

static bool is_typeable(int field)
{
    return field == EDIT_START || field == EDIT_SIZE || field == EDIT_END;
}

static Color tone_color(StatusTone tone)
{
    if (tone == STATUS_TONE_OFF)
    {
        return COLOR_GREY;
    }
    return (tone == STATUS_TONE_KEEPS) ? COLOR_GREEN : COLOR_YELLOW;
}

PartitionEditorScreen::PartitionEditorScreen(const ScreenTools &tools, const EditorSources &sources)
    : m_ui(tools.ui), m_input(tools.input), m_dialogs(tools.dialogs), m_session(sources.session), m_filesystems(sources.filesystems),
      m_settings(sources.settings), m_number_pad(tools), m_layout(&g_Layout480), m_number_pad_view(NULL)
{
}

void PartitionEditorScreen::Configure(const Layout *layout)
{
    m_layout = layout;
    m_number_pad.Configure(layout);
}

void PartitionEditorScreen::DrawUnderNumberPad()
{
    if (m_number_pad_view != NULL)
    {
        DrawPanel(*m_number_pad_view);
    }
}

void PartitionEditorScreen::Run(int part)
{
    // What a resize can keep depends on how much is in use
    if (m_settings.ResizeKeepsData())
    {
        m_filesystems.EnsureUsage(part);
    }

    EditState state;
    state.part = part;
    state.field = EDIT_SIZE;
    state.step = EDIT_DEFAULT_STEP;
    state.message.text = NULL; // One-off feedback
    state.message.color = COLOR_RED;
    state.bar_y = (FLOAT)FieldTop(state.field);
    state.slide_tick = 0;

    m_dialogs.AnimatePanelOpen(PanelBox());
    m_input.WaitForRelease();

    while (Step(state))
    {
    }
    Leave(part);
}

bool PartitionEditorScreen::Step(EditState &state)
{
    UpdateAvailableFields(state);
    SlideTowards(state.bar_y, (FLOAT)FieldTop(state.field), state.slide_tick);

    m_dialogs.BeginPanelFrame();
    DrawPanel(state);
    m_ui.EndFrame();

    return !m_input.Read() || HandleButtons(state);
}

// A disabled partition has only Enabled, Step and Back
void PartitionEditorScreen::UpdateAvailableFields(EditState &state) const
{
    const bool enabled = m_session.Edited().IsPartitionEnabled(state.part);
    for (int i = 0; i < EDIT_FIELD_COUNT; i++)
    {
        state.field_available[i] = true;
    }
    state.field_available[EDIT_START] = enabled;
    state.field_available[EDIT_SIZE] = enabled;
    state.field_available[EDIT_END] = enabled;
    state.field_available[EDIT_FILL] = enabled;
    if (!state.field_available[state.field])
    {
        state.field = EDIT_ENABLED;
    }
}

// Up/Down picks a field, Left/Right changes it, A applies or types a value,
// Y resets the partition, B or Start leaves
bool PartitionEditorScreen::HandleButtons(EditState &state)
{
    const int buttons = m_input.DigitalButtons();
    const int analog = m_input.AnalogButton();

    const int vertical = (buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
    const int field_move = state.field_repeat.Step(vertical);
    if (vertical != 0)
    {
        if (field_move != 0)
        {
            MoveField(state, field_move);
        }
        return true;
    }

    if ((buttons & XINPUT_GAMEPAD_START) || analog == XINPUT_GAMEPAD_B)
    {
        return false;
    }

    if (analog == XINPUT_GAMEPAD_Y)
    {
        ResetPartition(state);
        return true;
    }

    const int horizontal = (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : (buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : 0;
    const FieldInput input = {state.change_repeat.Step(horizontal), horizontal, analog == XINPUT_GAMEPAD_A};
    if (horizontal == 0 && !input.apply)
    {
        return true;
    }

    if (input.apply && is_typeable(state.field))
    {
        TypeValue(state);
        return true;
    }
    return ChangeField(state, input);
}

bool PartitionEditorScreen::ChangeField(EditState &state, const FieldInput &input)
{
    switch (state.field)
    {
    case EDIT_ENABLED:
        ToggleEnabled(state);
        break;
    case EDIT_START:
        MoveStart(state, input.change);
        break;
    case EDIT_SIZE:
    case EDIT_END: // Same as the size: the start stays put
        ChangeSize(state, input.change);
        break;
    case EDIT_STEP:
        ChangeStep(state, input.horizontal);
        break;
    case EDIT_FILL:
        FillFreeSpace(state, input.apply);
        break;
    case EDIT_BACK:
        return !input.apply;
    }
    return true;
}

void PartitionEditorScreen::MoveField(EditState &state, int move)
{
    // Enabled is always available, so this ends
    const int step = (move < 0) ? EDIT_FIELD_COUNT - 1 : 1;
    do
    {
        state.field = (state.field + step) % EDIT_FIELD_COUNT;
    } while (!state.field_available[state.field]);
    state.message.text = NULL;
}

void PartitionEditorScreen::ResetPartition(EditState &state)
{
    if (m_session.Edited().RestorePartitionEntry(state.part, m_session.InUse().GetPartitionEntry(state.part)))
    {
        state.message.text = L"Partition reset to the saved table.";
        state.message.color = COLOR_GREEN;
    }
    else
    {
        state.message.text = L"Can't reset: other partitions now use that space.";
        state.message.color = COLOR_RED;
    }
    m_input.WaitForRelease();
}

void PartitionEditorScreen::ToggleEnabled(EditState &state)
{
    state.message.text = NULL;
    if (m_session.Edited().IsPartitionEnabled(state.part))
    {
        m_session.Edited().DisablePartition(state.part);
    }
    else
    {
        m_session.Edited().EnablePartition(state.part);
        if (!m_session.Edited().IsPartitionEnabled(state.part))
        {
            state.message.text = L"No free space for this partition.";
            state.message.color = COLOR_RED;
        }
    }
    m_input.WaitForRelease();
}

// The end stays: forward shrinks, back grows. change is 0 between repeats.
void PartitionEditorScreen::MoveStart(EditState &state, int change)
{
    if (change > 0)
    {
        state.message.text = NULL;
        m_session.Edited().PartitionStartForward(state.part, g_EditSteps[state.step]);
    }
    else if (change < 0)
    {
        state.message.text = NULL;
        m_session.Edited().PartitionStartBack(state.part, g_EditSteps[state.step]);
    }
}

void PartitionEditorScreen::ChangeSize(EditState &state, int change)
{
    if (change > 0)
    {
        state.message.text = NULL;
        m_session.Edited().PartitionIncreaseSizeBy(state.part, g_EditSteps[state.step]);
    }
    else if (change < 0)
    {
        state.message.text = NULL;
        m_session.Edited().PartitionDecreaseSizeBy(state.part, g_EditSteps[state.step]);
    }
}

// A or Right: bigger steps, Left: smaller
void PartitionEditorScreen::ChangeStep(EditState &state, int horizontal)
{
    state.message.text = NULL;
    state.step = (state.step + (horizontal < 0 ? EDIT_STEP_COUNT - 1 : 1)) % EDIT_STEP_COUNT;
    m_input.WaitForRelease();
}

// As far as it can (the table editor clamps)
void PartitionEditorScreen::FillFreeSpace(EditState &state, bool apply)
{
    if (apply)
    {
        state.message.text = NULL;
        m_session.Edited().PartitionIncreaseSizeBy(state.part, (uint64_t)-1);
    }
    m_input.WaitForRelease();
}

void PartitionEditorScreen::TypeValue(EditState &state)
{
    // The number pad draws this screen behind it
    m_number_pad_view = &state;
    ULONGLONG value;
    if (m_number_pad.Run(ValueRequest(state.part, state.field), &value))
    {
        state.message.text = ApplyTypedValue(state.part, state.field, value);
        state.message.color = COLOR_YELLOW;
    }
    m_number_pad_view = NULL;
}

// Also limited by the maximum size (1TB on v3)
// Sectors in hex, like the table. Growing is limited by the free space and
// the maximum size (1TB on v3).
NumberInputRequest PartitionEditorScreen::ValueRequest(int part, int field) const
{
    const PartitionTableEditor &edited = m_session.Edited();
    const ULONGLONG start = edited.GetPartitionStart(part);
    const ULONGLONG size = edited.GetPartitionSize(part);
    const ULONGLONG end = start + size;

    if (field == EDIT_SIZE)
    {
        const NumberInputRequest request = {L"Size", size, 0, size + edited.GetGrowthLimit(part), INPUT_UNIT_GB};
        return request;
    }
    if (field == EDIT_END)
    {
        const NumberInputRequest request = {L"End sector", end, start, end + edited.GetGrowthLimit(part), INPUT_UNIT_SECTORS_HEX};
        return request;
    }

    // Up to one 4 KB block before the end. A zero size partition moves instead.
    NumberInputRequest request = {L"Start sector", start, 0, start, INPUT_UNIT_SECTORS_HEX};
    request.min = start - ((size == 0) ? edited.GetAvailableFreeSpaceForPartitionBefore(part) : edited.GetGrowthLimitBefore(part));
    if (size == 0)
    {
        request.max = start + edited.GetAvailableFreeSpaceForPartition(part);
    }
    else if (size > LBA_ALIGNMENT)
    {
        request.max = edited.AlignTo4k(end - LBA_ALIGNMENT);
    }
    return request;
}

// Only the alignment can change it (the number pad checked the range)
LPCWSTR PartitionEditorScreen::ApplyTypedValue(int part, int field, ULONGLONG value)
{
    PartitionTableEditor &edited = m_session.Edited();
    const ULONGLONG start = edited.GetPartitionStart(part);
    const ULONGLONG size = edited.GetPartitionSize(part);

    ULONGLONG result;
    if (field == EDIT_START)
    {
        edited.MovePartitionStartTo(part, value);
        result = edited.GetPartitionStart(part);
        // Unless the end wasn't 4K aligned, or an empty partition moved
        if (size != 0 && result + edited.GetPartitionSize(part) != start + size)
        {
            return L"Start and end rounded to 4 KB boundaries.";
        }
    }
    else if (field == EDIT_SIZE)
    {
        edited.ResizePartitionTo(part, value);
        result = edited.GetPartitionSize(part);
    }
    else
    {
        edited.ResizePartitionTo(part, value - start);
        result = start + edited.GetPartitionSize(part);
    }
    return (result != value) ? L"Rounded to a 4 KB boundary." : NULL;
}

void PartitionEditorScreen::Leave(int part)
{
    // A partition left empty is disabled
    m_session.DisableIfEmpty(part);

    m_input.WaitForRelease();

    m_dialogs.AnimatePanelClose(PanelBox());
}

void PartitionEditorScreen::DrawPanel(const EditState &state)
{
    const PanelGeometry geometry = Geometry();
    const PanelRect box = {geometry.left, geometry.top, geometry.right, geometry.bottom};

    wchar_t title[40];
    swprintf(title, L"Edit partition %d (%c)", state.part + 1, PartitionLetter(state.part));
    m_dialogs.DrawPanel(box, title, geometry.first_item_y);

    // Before the text, so it never covers it
    DrawSelectionBar(state, geometry);
    DrawFields(state, geometry);
    DrawDivider(geometry);
    DrawDetails(state.part, geometry);
    DrawFooter(state, box);
}

void PartitionEditorScreen::DrawSelectionBar(const EditState &state, const PanelGeometry &geometry)
{
    const int selection_top = (int)(state.bar_y + 0.5f);
    m_ui.Rect((FLOAT)(geometry.left + m_layout->menu_selection_inset),
              (FLOAT)selection_top,
              (FLOAT)(geometry.right - m_layout->menu_selection_inset),
              (FLOAT)(selection_top + geometry.bar_height),
              Colors().selection,
              Colors().selection);
}

void PartitionEditorScreen::DrawFields(const EditState &state, const PanelGeometry &geometry)
{
    for (int i = 0; i < EDIT_FIELD_COUNT; i++)
    {
        wchar_t text[120];
        FormatFieldValue(state, i, text);

        Color color = state.field_available[i] ? COLOR_WHITE : COLOR_GREY;
        if (i == state.field)
        {
            color = COLOR_YELLOW;
        }

        // Both fonts have the same height, so they share a baseline
        const int bar_top = geometry.first_item_y + i * geometry.item_height;
        const int bar_bottom = bar_top + geometry.bar_height;
        m_ui.Text(FONT_TEXT, g_FieldLabels[i], geometry.label_x, m_ui.TextYCentered(FONT_TEXT, bar_top, bar_bottom), color);
        m_ui.Text(FONT_TABLE, text, geometry.value_x, m_ui.TextYCentered(FONT_TABLE, bar_top, bar_bottom), color);
    }
}

void PartitionEditorScreen::FormatFieldValue(const EditState &state, int field, wchar_t *text)
{
    const ULONGLONG start = m_session.Edited().GetPartitionStart(state.part);
    const ULONGLONG size = m_session.Edited().GetPartitionSize(state.part);
    wchar_t human[HUMAN_SIZE_TEXT_COUNT];
    wchar_t lba[TABLE_LBA_TEXT_COUNT];
    text[0] = L'\0';

    switch (field)
    {
    case EDIT_ENABLED:
        swprintf(text, m_session.Edited().IsPartitionEnabled(state.part) ? L"yes" : L"no");
        break;
    case EDIT_START:
    case EDIT_END: {
        const ULONGLONG sector = (field == EDIT_START) ? start : start + size;
        FormatHumanSize(sector, human);
        FormatTableLba(sector, lba);
        swprintf(text, L"%s (at %s)", lba, human);
        break;
    }
    case EDIT_SIZE:
        FormatHumanSize(size, human);
        swprintf(text, L"%s (%I64u sectors)", human, size);
        break;
    case EDIT_STEP:
        swprintf(text, L"< %s >", g_EditStepNames[state.step]);
        break;
    case EDIT_FILL:
        // What filling adds: the free space, up to the maximum size
        FormatHumanSize(m_session.Edited().GetGrowthLimit(state.part), human);
        swprintf(text, L"+%s", human);
        break;
    }
}

void PartitionEditorScreen::DrawDivider(const PanelGeometry &geometry)
{
    m_ui.Rect((FLOAT)(geometry.left + m_layout->menu_selection_inset),
              (FLOAT)geometry.divider_y,
              (FLOAT)(geometry.right - m_layout->menu_selection_inset),
              (FLOAT)(geometry.divider_y + 1),
              Colors().panel_line,
              Colors().panel_line);
}

void PartitionEditorScreen::DrawDetails(int part, const PanelGeometry &geometry)
{
    const DetailRow details[EDIT_DETAIL_COUNT] = {NameDetail(part), ClusterDetail(part), OnDiskDetail(part), FreeSpaceDetail(part), StatusDetail(part)};
    for (int d = 0; d < EDIT_DETAIL_COUNT; d++)
    {
        const int row_top = geometry.first_detail_y + d * geometry.item_height;
        const int row_bottom = row_top + geometry.bar_height;
        m_ui.Text(FONT_TEXT, details[d].label, geometry.label_x, m_ui.TextYCentered(FONT_TEXT, row_top, row_bottom), COLOR_GREY);
        m_ui.Text(details[d].font, details[d].value, geometry.value_x, m_ui.TextYCentered(details[d].font, row_top, row_bottom), details[d].color);
    }
}

// 16 single byte characters, space padded
PartitionEditorScreen::DetailRow PartitionEditorScreen::NameDetail(int part) const
{
    DetailRow row = {L"Name", L"", FONT_TABLE, COLOR_WHITE};
    const XboxPartitionTableEntry entry = m_session.Edited().GetPartitionEntry(part);
    for (int c = 0; c < (int)XboxPartitionNameMaxLength; c++)
    {
        row.value[c] = (entry.Name[c] >= 0x20 && entry.Name[c] < 0x7F) ? (wchar_t)entry.Name[c] : L' ';
    }
    row.value[XboxPartitionNameMaxLength] = L'\0';
    return row;
}

PartitionEditorScreen::DetailRow PartitionEditorScreen::ClusterDetail(int part) const
{
    DetailRow row = {L"Format uses", L"", FONT_TEXT, COLOR_WHITE};
    swprintf(row.value, L"%u KB clusters", m_session.Edited().CalculateClusterSizeForPartition(part));
    return row;
}

PartitionEditorScreen::DetailRow PartitionEditorScreen::OnDiskDetail(int part) const
{
    DetailRow row = {L"On disk now", L"", FONT_TEXT, COLOR_WHITE};
    const unsigned int cluster_kb = m_filesystems.ClusterSizeKb(part);
    if (cluster_kb == 0)
    {
        swprintf(row.value, L"not formatted (no FATX)");
    }
    else if (m_filesystems.Usage(part).IsKnown())
    {
        wchar_t used[HUMAN_SIZE_TEXT_COUNT];
        const FatxUsage &usage = m_filesystems.Usage(part).Get();
        FormatHumanSize(usage.used_clusters * usage.cluster_bytes / HDD_SECTOR_SIZE, used);
        swprintf(row.value, L"FATX, %u KB clusters, %s used", cluster_kb, used);
    }
    else
    {
        swprintf(row.value, L"FATX, %u KB clusters", cluster_kb);
    }
    return row;
}

PartitionEditorScreen::DetailRow PartitionEditorScreen::FreeSpaceDetail(int part) const
{
    DetailRow row = {L"Free space", L"", FONT_TEXT, COLOR_WHITE};
    const bool enabled = m_session.Edited().IsPartitionEnabled(part);
    wchar_t before[HUMAN_SIZE_TEXT_COUNT];
    wchar_t after[HUMAN_SIZE_TEXT_COUNT];
    FormatHumanSize(enabled ? m_session.Edited().GetAvailableFreeSpaceForPartitionBefore(part) : 0, before);
    FormatHumanSize(enabled ? m_session.Edited().GetAvailableFreeSpaceForPartition(part) : 0, after);
    swprintf(row.value, L"%s before, %s after", before, after);
    return row;
}

PartitionEditorScreen::DetailRow PartitionEditorScreen::StatusDetail(int part) const
{
    const PartitionStatusQuery query = {m_session.Edited(), m_session.InUse(), m_filesystems.Usage(part), m_settings.ResizeKeepsData()};
    const PartitionStatus status = DescribePartitionStatus(query, part);
    DetailRow row = {L"Status", L"", FONT_TEXT, tone_color(status.tone)};
    wcscpy(row.value, status.text);
    return row;
}

void PartitionEditorScreen::DrawFooter(const EditState &state, const PanelRect &box)
{
    if (state.message.text != NULL)
    {
        m_dialogs.DrawPanelFooter(box, state.message.text, state.message.color);
    }
    else if (is_typeable(state.field))
    {
        m_dialogs.DrawPanelFooter(box, L"Left/Right: change   A: type it in   Y: reset   B: back");
    }
    else
    {
        m_dialogs.DrawPanelFooter(box, L"Left/Right: change   A: apply   Y: reset   B: back");
    }
}

PartitionEditorScreen::PanelGeometry PartitionEditorScreen::Geometry() const
{
    PanelGeometry geometry;
    geometry.left = m_layout->edit_left;
    geometry.right = m_layout->edit_right;
    geometry.top = PanelTop();
    geometry.bar_height = m_dialogs.MenuItemBarHeight();
    geometry.item_height = m_dialogs.MenuItemHeight();
    geometry.first_item_y = geometry.top + m_layout->menu_first_item_offset;
    geometry.divider_y = geometry.first_item_y + EDIT_FIELD_COUNT * geometry.item_height + geometry.item_height / 4;
    geometry.first_detail_y = geometry.first_item_y + EDIT_FIELD_COUNT * geometry.item_height + geometry.item_height / 2;
    geometry.bottom = geometry.first_detail_y + EDIT_DETAIL_COUNT * geometry.item_height + m_layout->menu_footer_height;
    geometry.label_x = geometry.left + m_layout->menu_text_inset;
    geometry.value_x = geometry.left + m_layout->info_value_offset;
    return geometry;
}

int PartitionEditorScreen::FieldTop(int field) const
{
    return PanelTop() + m_layout->menu_first_item_offset + field * m_dialogs.MenuItemHeight();
}

int PartitionEditorScreen::PanelHeight() const
{
    const int item_height = m_dialogs.MenuItemHeight();
    return m_layout->menu_first_item_offset + (EDIT_FIELD_COUNT + EDIT_DETAIL_COUNT) * item_height + item_height / 2 + m_layout->menu_footer_height;
}

int PartitionEditorScreen::PanelTop() const
{
    return (m_layout->height - PanelHeight()) / 2;
}

PanelRect PartitionEditorScreen::PanelBox() const
{
    const PanelRect box = {m_layout->edit_left, PanelTop(), m_layout->edit_right, PanelTop() + PanelHeight()};
    return box;
}

#include "stdafx.h"
#include "main_screen.h"
#include "array_length.h"
#include "partition_format.h"
#include "selection_slide.h"
#include <stdio.h>
#include <string.h>

// Table panel opacity (0-255), below the menus' so the background shows through
static const DWORD TABLE_PANEL_ALPHA = 190;

MainScreen::MainScreen(Renderer &ui, const MainScreenSources &sources)
    : m_ui(ui), m_session(sources.session), m_tables(sources.tables), m_filesystems(sources.filesystems), m_layout(&g_Layout480),
      m_selected(0), m_bar_y(0.0f), m_bar_placed(false), m_bar_tick(0)
{
}

void MainScreen::Configure(const Layout *layout)
{
    m_layout = layout;
}

void MainScreen::Select(int partition)
{
    m_selected = partition;
}

void MainScreen::MoveSelection(int direction)
{
    const int partition = m_selected + m_repeat.Step(direction);
    if (partition >= 0 && partition < XboxPartitionTableEntryCount)
    {
        m_selected = partition;
    }
}

void MainScreen::UpdateSelectionBar()
{
    const FLOAT target = (FLOAT)TableRowTop(m_selected);

    if (!m_bar_placed)
    {
        m_bar_y = target;
        m_bar_placed = true;
        return;
    }

    SlideTowards(m_bar_y, target, m_bar_tick);
}

int MainScreen::TableCenterX() const
{
    return (m_layout->table_left + m_layout->table_right) / 2;
}

int MainScreen::TableSlotTop(int slot) const
{
    return m_layout->table_first_row_y + slot * m_layout->table_row_height + (slot > 0 ? m_layout->table_header_gap : 0);
}

int MainScreen::StandardLabelSlot()
{
    return 1;
}

int MainScreen::ExtendedLabelSlot()
{
    return XBOX_STANDARD_PARTITION_COUNT + 2;
}

int MainScreen::TableRowTop(int partition) const
{
    return TableSlotTop(partition + (partition >= XBOX_STANDARD_PARTITION_COUNT ? 3 : 2));
}

void MainScreen::Draw()
{
    m_ui.DrawBackground();

    wchar_t title[100];
    FormatWide(title, ArrayLength(title), L"%s - LBA48 patch v%d", PROGRAM_NAME_AND_VERSION, m_tables.Lba48Version());
    m_ui.Text(FONT_TEXT, title, m_layout->title_x, m_layout->title_y, COLOR_WHITE);

    DrawLoadedTableName();
    DrawDiskMap();
    DrawTable();
    DrawFreeSpace();
}

void MainScreen::DrawTable()
{
    const int bar_height = m_ui.TextBoxHeight(FONT_TABLE, m_layout->table_row_padding);
    const int last_slot = XboxPartitionTableEntryCount + 2;
    const int left = m_layout->table_left;
    const int right = m_layout->table_right;
    const int top = m_layout->table_top;
    const int bottom = TableSlotTop(last_slot) + bar_height + (m_layout->table_first_row_y - m_layout->table_top);
    const int inset = m_layout->menu_selection_inset;
    const int label_x = left + m_layout->menu_text_inset;

    const D3DCOLOR panel = (Colors().panel & 0x00FFFFFF) | ((D3DCOLOR)TABLE_PANEL_ALPHA << 24);
    m_ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, panel, panel);
    m_ui.Outline((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel_border);

    // As wide as the rows, so the columns line up
    wchar_t row[TABLE_ROW_TEXT_COUNT];
    const int header_top = TableSlotTop(0);
    FormatTableHeader(row);
    m_ui.Text(FONT_TABLE, row, TableCenterX(), m_ui.TextYCentered(FONT_TABLE, header_top, header_top + bar_height), COLOR_GREY, XFONT_CENTER);

    const int line_y = (header_top + bar_height + TableSlotTop(1)) / 2;
    m_ui.Rect((FLOAT)(left + inset), (FLOAT)line_y, (FLOAT)(right - inset), (FLOAT)(line_y + 1), Colors().panel_line, Colors().panel_line);

    // Before the row text, so it never covers a row. Outside the main loop it
    // doesn't animate: until the first frame it sits on the row.
    const int selection_top = m_bar_placed ? (int)(m_bar_y + 0.5f) : TableRowTop(m_selected);
    m_ui.Rect((FLOAT)(left + inset), (FLOAT)selection_top, (FLOAT)(right - inset), (FLOAT)(selection_top + bar_height), Colors().selection, Colors().selection);

    const int standard_top = TableSlotTop(StandardLabelSlot());
    const int extended_top = TableSlotTop(ExtendedLabelSlot());
    m_ui.Text(FONT_TEXT,
              L"Standard partitions (custom sizes might cause problems)",
              label_x,
              m_ui.TextYCentered(FONT_TEXT, standard_top, standard_top + bar_height),
              COLOR_GREY);
    m_ui.Text(FONT_TEXT, L"Extended partitions", label_x, m_ui.TextYCentered(FONT_TEXT, extended_top, extended_top + bar_height), COLOR_GREY);

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const int bar_top = TableRowTop(i);
        FormatPartitionRow(i, row);

        Color text_color = m_session.Edited().IsPartitionEnabled(i) ? COLOR_WHITE : COLOR_GREY;
        if (m_selected == i)
        {
            text_color = COLOR_YELLOW;
        }

        // Digits and capitals only, so centering on the capitals is exact
        m_ui.Text(FONT_TABLE, row, TableCenterX(), m_ui.TextYCentered(FONT_TABLE, bar_top, bar_top + bar_height), text_color, XFONT_CENTER);
    }
}

// From the theme's first map color to its second, every other one darker,
// so neighbours always differ
static D3DCOLOR disk_map_color(D3DCOLOR first, D3DCOLOR last, int k, int count)
{
    const int t256 = (count > 1) ? (k * 256) / (count - 1) : 0;
    const int shade = (k % 2 == 0) ? 256 : 176; // Of 256
    int channels[3];
    for (int i = 0; i < 3; i++)
    {
        const int shift = 16 - 8 * i;
        const int a = (int)((first >> shift) & 0xFF);
        const int b = (int)((last >> shift) & 0xFF);
        const int mixed = a + ((b - a) * t256) / 256;
        channels[i] = (mixed * shade) / 256;
    }
    return D3DCOLOR_XRGB(channels[0], channels[1], channels[2]);
}

// Partitions to scale, from the first to the last usable sector. One ending
// within 1 MB of the end reaches the edge, so a full disk looks full.
void MainScreen::DrawDiskMap()
{
    const FLOAT left = (FLOAT)m_layout->table_left;
    const FLOAT right = (FLOAT)m_layout->table_right;
    const FLOAT top = (FLOAT)m_layout->map_y;
    const FLOAT bottom = (FLOAT)(m_layout->map_y + m_layout->map_height);

    m_ui.Rect(left, top, right, bottom, Colors().map_free, Colors().map_free);
    m_ui.Outline(left, top, right, bottom, Colors().panel_border);

    DiskMapArea area = {left + 1.0f, right - 1.0f, top + 1.0f, bottom - 1.0f, XBOX_SWAPPART1_LBA_START, 0, 0.0};
    area.end = area.start + m_session.Edited().GetUsableLBAs();
    if (area.end <= area.start)
    {
        return;
    }
    area.pixels_per_sector = (double)(area.right - area.left) / (double)(area.end - area.start);

    // Colored by order on the disk; the selected partition last, so it is never covered
    const int shown = CountDiskMapPartitions();
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (i != m_selected && IsOnDiskMap(i))
        {
            DrawDiskMapPartition(i, area, disk_map_color(Colors().map_standard, Colors().map_extended, DiskMapPlace(i), shown));
        }
    }
    if (IsOnDiskMap(m_selected))
    {
        DrawDiskMapPartition(m_selected, area, Colors().map_selected);
    }
}

bool MainScreen::IsOnDiskMap(int partition) const
{
    return m_session.Edited().IsPartitionEnabled(partition) && m_session.Edited().GetPartitionSize(partition) != 0;
}

int MainScreen::CountDiskMapPartitions() const
{
    int count = 0;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        count += IsOnDiskMap(i) ? 1 : 0;
    }
    return count;
}

int MainScreen::DiskMapPlace(int partition) const
{
    const ULONGLONG start = m_session.Edited().GetPartitionStart(partition);
    int place = 0;
    for (int j = 0; j < XboxPartitionTableEntryCount; j++)
    {
        const ULONGLONG other = m_session.Edited().GetPartitionStart(j);
        if (j != partition && IsOnDiskMap(j) && (other < start || (other == start && j < partition)))
        {
            place++;
        }
    }
    return place;
}

void MainScreen::DrawDiskMapPartition(int partition, const DiskMapArea &area, D3DCOLOR color)
{
    // Smaller gaps at the end don't show (alignment, the backup table)
    static const ULONGLONG MAP_END_SNAP = 0x800; // 1 MB

    // Clamped to the map, the end never before the start (damaged or foreign tables)
    const ULONGLONG start = m_session.Edited().GetPartitionStart(partition);
    const ULONGLONG end = start + m_session.Edited().GetPartitionSize(partition);
    const ULONGLONG part_start = (start < area.start) ? area.start : (start > area.end) ? area.end : start;
    const ULONGLONG part_end = (end < part_start) ? part_start : (end > area.end) ? area.end : end;
    const bool at_end = (part_end + MAP_END_SNAP >= area.end);

    const FLOAT x1 = area.left + (FLOAT)((double)(part_start - area.start) * area.pixels_per_sector);
    FLOAT x2 = at_end ? area.right : area.left + (FLOAT)((double)(part_end - area.start) * area.pixels_per_sector);
    if (x2 - x1 < 1.0f)
    {
        x2 = x1 + 1.0f;
    }
    m_ui.Rect(x1, area.top, x2, area.bottom, color, color);
}

// Fixed width rows (52 characters), so centering keeps the columns aligned:
//   "Part  Start         End                Size  FATX    "
//   " 6 F  000000EE8AB0  0000E8E08000  931.51 GB  64KB  *"
// Marker against the written table: * changed, + new, - removed.
void MainScreen::FormatTableHeader(wchar_t *row)
{
    FormatWide(row, TABLE_ROW_TEXT_COUNT, L"%-4s  %-12s  %-12s  %9s  %-5s  ", L"Part", L"Start", L"End", L"Size", L"FATX");
}

void MainScreen::FormatPartitionRow(const int partition_number, wchar_t *row)
{
    const ULONGLONG lba_start = m_session.Edited().GetPartitionStart(partition_number);
    const ULONGLONG lba_size = m_session.Edited().GetPartitionSize(partition_number);
    const bool enabled = m_session.Edited().IsPartitionEnabled(partition_number);
    // Before a table is loaded there is nothing to compare with
    const bool was_enabled = m_session.InUse().IsPartitionEnabled(partition_number);
    const wchar_t letter = PartitionLetter(partition_number);

    if (!enabled && !was_enabled)
    {
        FormatWide(row, TABLE_ROW_TEXT_COUNT, L"%2d %c  %-46s", partition_number + 1, letter, L"unused");
        return;
    }

    wchar_t marker = L' ';
    if (!enabled)
    {
        marker = L'-';
    }
    else if (!was_enabled)
    {
        marker = L'+';
    }
    else if (lba_start != m_session.InUse().GetPartitionStart(partition_number) || lba_size != m_session.InUse().GetPartitionSize(partition_number))
    {
        marker = L'*';
    }

    wchar_t start_str[16];
    wchar_t end_str[16];
    wchar_t size_str[20];
    FormatTableLba(lba_start, start_str);
    FormatTableLba(lba_start + lba_size, end_str);
    FormatHumanSize(lba_size, size_str);

    // FATX cluster size on disk; ERROR if too small for the size, blank if none
    const unsigned int cluster_size = m_filesystems.ClusterSizeKb(partition_number);
    wchar_t cluster_str[16];
    if (cluster_size == 0 || lba_size == 0)
    {
        FormatWide(cluster_str, ArrayLength(cluster_str), L"");
    }
    else if (cluster_size < m_session.Edited().CalculateClusterSize(lba_size))
    {
        FormatWide(cluster_str, ArrayLength(cluster_str), L"ERROR");
    }
    else
    {
        FormatWide(cluster_str, ArrayLength(cluster_str), L"%uKB", cluster_size);
    }

    FormatWide(row, TABLE_ROW_TEXT_COUNT, L"%2d %c  %-12s  %-12s  %9s  %-5s %c", partition_number + 1, letter, start_str, end_str, size_str, cluster_str, marker);
}

void MainScreen::DrawFreeSpace()
{
    // Usable: after the stock start and before the backup table
    const uint64_t usable_lbas = m_session.Edited().GetUsableLBAs();
    const uint64_t free_lbas = m_session.Edited().GetAvailableLBAs();

    wchar_t free_size[32];
    wchar_t usable_size[32];
    FormatHumanSize(free_lbas, free_size);
    FormatHumanSize(usable_lbas, usable_size);

    wchar_t size_line[100];
    FormatWide(size_line, ArrayLength(size_line), L"Free: %s of %s", free_size, usable_size);
    m_ui.Text(FONT_TEXT, size_line, m_layout->margin_x, m_layout->free_space_y, COLOR_WHITE);

    m_ui.Text(FONT_TEXT, L"* changed   + new   - removed", m_layout->table_right - m_layout->menu_text_inset, m_layout->legend_y, COLOR_GREY, XFONT_RIGHT);
}

void MainScreen::DrawLoadedTableName()
{
    // Indexed by LoadedTable
    static const LPCWSTR names[LOADED_TABLE_PRESET + 1] = {
        L"Current table [Kernel]",
        L"Current table [HDD]",
        L"Current table [Backup]",
        L"Current table [Preset]",
    };

    if (m_session.Source() >= 0 && m_session.Source() <= LOADED_TABLE_PRESET)
    {
        m_ui.Text(FONT_TEXT, names[m_session.Source()], m_layout->loaded_table_x, m_layout->title_y, COLOR_WHITE);
    }
}

void MainScreen::DrawControlsHint()
{
    m_ui.Text(FONT_TEXT, L"D-pad: select partition    A: edit    Start: menu", m_layout->margin_x, m_layout->prompt_y, COLOR_WHITE);
}

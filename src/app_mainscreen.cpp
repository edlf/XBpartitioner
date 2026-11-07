// The main screen, also drawn behind every other screen.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// Table panel opacity (0-255), below the menus' so the background shows through
static const DWORD TABLE_PANEL_ALPHA = 190;

// All but the status and prompt lines
void App::DrawMainScreen()
{
    m_Ui.DrawBackground();

    wchar_t title[100];
    swprintf(title, L"XBpartitioner " XBPARTITIONER_VERSION L" - LBA48 patch v%d", m_Lba48Version);
    m_Ui.Text(FONT_TEXT, title, m_Layout->title_x, m_Layout->title_y, COLOR_WHITE);

    DrawLoadedTableName();
    DrawDiskMap();
    DrawTable();
    DrawFreeSpace();
}

void App::DrawTable()
{
    const int bar_height = m_Ui.TextBoxHeight(FONT_TABLE, m_Layout->table_row_padding);
    const int last_slot = XboxPartitionTableEntryCount + 2;
    const int left = m_Layout->table_left;
    const int right = m_Layout->table_right;
    const int top = m_Layout->table_top;
    const int bottom = TableSlotTop(last_slot) + bar_height + (m_Layout->table_first_row_y - m_Layout->table_top);
    const int inset = m_Layout->menu_selection_inset;
    const int label_x = left + m_Layout->menu_text_inset;

    const D3DCOLOR panel = (Colors().panel & 0x00FFFFFF) | ((D3DCOLOR)TABLE_PANEL_ALPHA << 24);
    m_Ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, panel, panel);
    m_Ui.Outline((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel_border);

    // As wide as the rows, so the columns line up
    wchar_t row[80];
    const int header_top = TableSlotTop(0);
    FormatTableHeader(row);
    m_Ui.Text(FONT_TABLE, row, TableCenterX(), m_Ui.TextYCentered(FONT_TABLE, header_top, header_top + bar_height), COLOR_GREY, XFONT_CENTER);

    const int line_y = (header_top + bar_height + TableSlotTop(1)) / 2;
    m_Ui.Rect((FLOAT)(left + inset), (FLOAT)line_y, (FLOAT)(right - inset), (FLOAT)(line_y + 1),
              Colors().panel_line, Colors().panel_line);

    // Before the row text, so it never covers a row. Outside the main loop it
    // doesn't animate: until the first frame it sits on the row.
    const int selection_top = m_SelectionBarPlaced ? (int)(m_SelectionBarY + 0.5f) : TableRowTop(m_CurrentPart);
    m_Ui.Rect((FLOAT)(left + inset), (FLOAT)selection_top, (FLOAT)(right - inset), (FLOAT)(selection_top + bar_height),
              Colors().selection, Colors().selection);

    const int standard_top = TableSlotTop(StandardLabelSlot());
    const int extended_top = TableSlotTop(ExtendedLabelSlot());
    m_Ui.Text(FONT_TEXT, L"Standard partitions (custom sizes might cause problems)", label_x,
              m_Ui.TextYCentered(FONT_TEXT, standard_top, standard_top + bar_height), COLOR_GREY);
    m_Ui.Text(FONT_TEXT, L"Extended partitions", label_x,
              m_Ui.TextYCentered(FONT_TEXT, extended_top, extended_top + bar_height), COLOR_GREY);

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const int bar_top = TableRowTop(i);
        FormatPartitionRow(i, row);

        Color text_color = m_pTable->isPartitionEnabled(i) ? COLOR_WHITE : COLOR_GREY;
        if (m_CurrentPart == i)
        {
            text_color = COLOR_YELLOW;
        }

        // Digits and capitals only, so centering on the capitals is exact
        m_Ui.Text(FONT_TABLE, row, TableCenterX(), m_Ui.TextYCentered(FONT_TABLE, bar_top, bar_top + bar_height), text_color, XFONT_CENTER);
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
void App::DrawDiskMap()
{
    const FLOAT left = (FLOAT)m_Layout->table_left;
    const FLOAT right = (FLOAT)m_Layout->table_right;
    const FLOAT top = (FLOAT)m_Layout->map_y;
    const FLOAT bottom = (FLOAT)(m_Layout->map_y + m_Layout->map_height);

    m_Ui.Rect(left, top, right, bottom, Colors().map_free, Colors().map_free);
    m_Ui.Outline(left, top, right, bottom, Colors().panel_border);

    const ULONGLONG map_start = XBOX_SWAPPART1_LBA_START;
    const ULONGLONG map_end = map_start + m_pTable->GetUsableLBAs();
    if (map_end <= map_start)
    {
        return;
    }
    // Smaller gaps at the end don't show (alignment, the backup table)
    static const ULONGLONG MAP_END_SNAP = 0x800; // 1 MB

    const FLOAT inner_left = left + 1.0f;
    const FLOAT inner_right = right - 1.0f;
    const double scale = (double)(inner_right - inner_left) / (double)(map_end - map_start);

    // Order by start, for the colors
    int place[XboxPartitionTableEntryCount];
    int shown = 0;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        place[i] = -1;
        if (!m_pTable->isPartitionEnabled(i) || m_pTable->GetPartitionSize(i) == 0)
        {
            continue;
        }
        place[i] = 0;
        for (int j = 0; j < XboxPartitionTableEntryCount; j++)
        {
            if (j != i && m_pTable->isPartitionEnabled(j) && m_pTable->GetPartitionSize(j) != 0 &&
                (m_pTable->GetPartitionStart(j) < m_pTable->GetPartitionStart(i) ||
                 (m_pTable->GetPartitionStart(j) == m_pTable->GetPartitionStart(i) && j < i)))
            {
                place[i]++;
            }
        }
        shown++;
    }

    // The selected partition last, so it is never covered
    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            const bool selected = (i == m_CurrentPart);
            if (selected != (pass == 1) || !m_pTable->isPartitionEnabled(i))
            {
                continue;
            }

            const ULONGLONG start = m_pTable->GetPartitionStart(i);
            const ULONGLONG size = m_pTable->GetPartitionSize(i);
            if (size == 0)
            {
                continue;
            }

            // Clamped to the map, the end never before the start (damaged or foreign tables)
            const ULONGLONG end = start + size;
            const ULONGLONG part_start = (start < map_start) ? map_start : (start > map_end) ? map_end : start;
            const ULONGLONG part_end = (end < part_start) ? part_start : (end > map_end) ? map_end : end;
            const bool at_end = (part_end + MAP_END_SNAP >= map_end);

            FLOAT x1 = inner_left + (FLOAT)((double)(part_start - map_start) * scale);
            FLOAT x2 = at_end ? inner_right : inner_left + (FLOAT)((double)(part_end - map_start) * scale);
            if (x2 - x1 < 1.0f)
            {
                x2 = x1 + 1.0f;
            }

            const D3DCOLOR color = selected ? Colors().map_selected
                                 : disk_map_color(Colors().map_standard, Colors().map_extended, place[i], shown);
            m_Ui.Rect(x1, top + 1.0f, x2, bottom - 1.0f, color, color);
        }
    }
}

// Fixed width rows (52 characters), so centering keeps the columns aligned:
//   "Part  Start         End                Size  FATX    "
//   " 6 F  000000EE8AB0  0000E8E08000  931.51 GB  64KB  *"
// Marker against the written table: * changed, + new, - removed.
void App::FormatTableHeader(wchar_t *row)
{
    swprintf(row, L"%-4s  %-12s  %-12s  %9s  %-5s  ", L"Part", L"Start", L"End", L"Size", L"FATX");
}

void App::FormatPartitionRow(const int partition_number, wchar_t *row)
{
    const ULONGLONG lba_start = m_pTable->GetPartitionStart(partition_number);
    const ULONGLONG lba_size = m_pTable->GetPartitionSize(partition_number);
    const bool enabled = m_pTable->isPartitionEnabled(partition_number);
    // Before a table is loaded there is nothing to compare with
    const bool was_enabled = m_pTableOriginal ? m_pTableOriginal->isPartitionEnabled(partition_number) : enabled;
    const wchar_t letter = partition_letter(partition_number);

    if (!enabled && !was_enabled)
    {
        swprintf(row, L"%2d %c  %-46s", partition_number + 1, letter, L"unused");
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
    else if (m_pTableOriginal &&
             (lba_start != m_pTableOriginal->GetPartitionStart(partition_number) ||
              lba_size != m_pTableOriginal->GetPartitionSize(partition_number)))
    {
        marker = L'*';
    }

    wchar_t start_str[16];
    wchar_t end_str[16];
    wchar_t size_str[20];
    format_table_lba(lba_start, start_str);
    format_table_lba(lba_start + lba_size, end_str);
    format_human_size(lba_size, size_str);

    // FATX cluster size on disk; ERROR if too small for the size, blank if none
    const unsigned int cluster_size = m_ClusterSizes[partition_number];
    wchar_t cluster_str[16];
    if (cluster_size == 0 || lba_size == 0)
    {
        swprintf(cluster_str, L"");
    }
    else if (cluster_size < m_pTable->CalculateClusterSize(lba_size))
    {
        swprintf(cluster_str, L"ERROR");
    }
    else
    {
        swprintf(cluster_str, L"%uKB", cluster_size);
    }

    swprintf(row, L"%2d %c  %-12s  %-12s  %9s  %-5s %c",
             partition_number + 1, letter, start_str, end_str, size_str, cluster_str, marker);
}

void App::DrawFreeSpace()
{
    // Usable: after the stock start and before the backup table
    const uint64_t usable_lbas = m_pTable->GetUsableLBAs();
    const uint64_t free_lbas = m_pTable->GetAvailableLBAs();

    wchar_t free_size[32];
    wchar_t usable_size[32];
    format_human_size(free_lbas, free_size);
    format_human_size(usable_lbas, usable_size);

    wchar_t size_line[100];
    swprintf(size_line, L"Free: %s of %s", free_size, usable_size);
    m_Ui.Text(FONT_TEXT, size_line, m_Layout->margin_x, m_Layout->free_space_y, COLOR_WHITE);

    m_Ui.Text(FONT_TEXT, L"* changed   + new   - removed", m_Layout->table_right - m_Layout->menu_text_inset,
              m_Layout->legend_y, COLOR_GREY, XFONT_RIGHT);
}

void App::DrawLoadedTableName()
{
    // Indexed by LoadedTable
    static const LPCWSTR names[LOADED_TABLE_PRESET + 1] = {
        L"Current table [Kernel]",
        L"Current table [HDD]",
        L"Current table [Backup]",
        L"Current table [Preset]",
    };

    if (m_LoadedTable >= 0 && m_LoadedTable <= LOADED_TABLE_PRESET)
    {
        m_Ui.Text(FONT_TEXT, names[m_LoadedTable], m_Layout->loaded_table_x, m_Layout->title_y, COLOR_WHITE);
    }
}

void App::DrawControlsHint()
{
    m_Ui.Text(FONT_TEXT, L"D-pad: select partition    A: edit    Start: menu", m_Layout->margin_x, m_Layout->prompt_y, COLOR_WHITE);
}

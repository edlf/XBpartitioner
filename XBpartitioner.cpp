#include "stdafx.h"
#include "xbpartitioner.h"

#include <io.h>
#include <stdio.h> //used for swprintf
#include <stdlib.h>
#include <string.h> //used for wcscat
#include <math.h>   // pow, for the selection bars
#include <winbase.h>
#include "Winioctl.h"
#include "prometheos_code.h"

// Full path of the running xbe, e.g. \Device\Harddisk0\Partition6\Apps\XBP\default.xbe
extern "C" __declspec(dllimport) ANSI_STRING XeImageFileName;

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// Compile time checks (no static_assert in this compiler): an array of size -1
// fails the build. The partition table is read and written as one raw sector.
typedef char check_table_size_is_one_sector[(XboxPartitionTableSize == HDD_SECTOR_SIZE) ? 1 : -1];
typedef char check_table_struct_size[(sizeof(XboxPartitionTable) == XboxPartitionTableSize) ? 1 : -1];

// Selection bars (main table, menus): fraction of the remaining distance they
// move per 1/60 s (about 0.15s to settle), and the D-pad repeat when held
static const FLOAT SELECTION_SLIDE_FACTOR = 0.35f;
static const FLOAT SLIDE_FRAME_MS = 1000.0f / 60.0f;
// Longest time step for one move: after a pause (another screen was shown)
// the bar continues instead of jumping
static const DWORD SLIDE_MAX_STEP_MS = 50;
static const DWORD REPEAT_DELAY_MS = 300; // Before a held D-pad starts repeating
static const DWORD REPEAT_RATE_MS = 120;  // Between repeats

// Opacity of the main screen's table panel (0-255); menus and dialogs use the
// theme's panel alpha (about 240)
static const DWORD TABLE_PANEL_ALPHA = 190;


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

void SlideTowards(FLOAT &position, FLOAT target, DWORD &last_tick)
{
    // By time, not by frame: screens with a lot of text take longer than a
    // frame to draw, and a per frame step then makes the bar crawl
    const DWORD now = GetTickCount();
    DWORD elapsed = (last_tick == 0) ? (DWORD)SLIDE_FRAME_MS : now - last_tick;
    last_tick = now;
    if (elapsed > SLIDE_MAX_STEP_MS)
    {
        elapsed = SLIDE_MAX_STEP_MS;
    }

    const FLOAT remaining = target - position;
    if (remaining > -0.5f && remaining < 0.5f)
    {
        position = target;
        return;
    }

    // (1 - factor) of the distance is left after each 1/60 s
    const FLOAT left = (FLOAT)pow(1.0 - SELECTION_SLIDE_FACTOR, (double)elapsed / SLIDE_FRAME_MS);
    position = target - remaining * left;
}

//-----------------------------------------------------------------------------
// Small formatting helpers
//-----------------------------------------------------------------------------

// Drive letter of a table slot: E, C, X, Y, Z for the stock partitions, then F, G, ...
static wchar_t partition_letter(int index)
{
    static const wchar_t stock_letters[XBOX_STANDARD_PARTITION_COUNT] = {L'E', L'C', L'X', L'Y', L'Z'};

    if (index < XBOX_STANDARD_PARTITION_COUNT)
    {
        return stock_letters[index];
    }
    return (wchar_t)(L'F' + (index - XBOX_STANDARD_PARTITION_COUNT));
}

// Sector count as "750.00 MB", "1.50 GB" or "1.82 TB": MB below 1000 MB, then
// GB, then TB (0x200000 sectors per GB, like the rest of the UI). At most 9
// characters, the partition table's size column relies on that.
static void format_human_size(ULONGLONG sectors, wchar_t *out)
{
    if (sectors < 1000ULL * 0x800)
    {
        swprintf(out, L"%.2f MB", (double)sectors / 0x800);
    }
    else if (sectors < 1000ULL * 0x200000)
    {
        swprintf(out, L"%.2f GB", (double)sectors / 0x200000);
    }
    else
    {
        swprintf(out, L"%.2f TB", (double)sectors / 0x80000000);
    }
}

// Table index of the partition the running xbe is on, from its path
// (\Device\Harddisk0\PartitionN\...), -1 if it isn't on the hard drive
static int launch_partition_index()
{
    static const char prefix[] = "\\Device\\Harddisk0\\Partition";
    const int prefix_length = sizeof(prefix) - 1;
    const char *path = XeImageFileName.Buffer;
    const int length = XeImageFileName.Length;
    if (path == NULL || length <= prefix_length || _strnicmp(path, prefix, prefix_length) != 0)
    {
        return -1;
    }

    int number = 0;
    int i = prefix_length;
    while (i < length && path[i] >= '0' && path[i] <= '9' && number <= XboxPartitionTableEntryCount)
    {
        number = number * 10 + (path[i] - '0');
        i++;
    }
    if (i == prefix_length || (i < length && path[i] != '\\') || number < 1 || number > XboxPartitionTableEntryCount)
    {
        return -1;
    }
    return number - 1;
}

//-----------------------------------------------------------------------------
// Startup and main loop
//-----------------------------------------------------------------------------

App::App()
    : m_Layout(&g_Layout480), m_pTable(NULL), m_pTableOriginal(NULL), m_pKernelTable(NULL),
      m_AnalogButton(-1), m_DigitalButtons(0),
      m_FullLba48(false), m_TotalHddSectors(0), m_Physical4k(false), m_BackupStatus(BACKUP_NOT_USED), m_Lba48Version(0),
      m_CurrentPart(0), m_SelectionBarY(0.0f), m_SelectionBarPlaced(false), m_SelectionBarTick(0),
      m_LoadedTable(LOADED_TABLE_KERNEL), m_pActiveEditView(NULL), m_WaitForRelease(false),
      m_LaunchPartition(-1), m_ThemeSaveBlocked(false)
{
    memset(m_ClusterSizes, 0, sizeof(m_ClusterSizes));
    memset(&m_TableKernel, 0, sizeof(m_TableKernel));
    memset(&m_TableHdd, 0, sizeof(m_TableHdd));
    memset(&m_TableHddBackup, 0, sizeof(m_TableHddBackup));
    memset(m_Controllers, 0, sizeof(m_Controllers));
    memset(&m_Drive, 0, sizeof(m_Drive));

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        RtlInitAnsiString(&m_PartitionPaths[i], partStr[i]);
    }

    m_LaunchPartition = launch_partition_index();

    m_Ui.Init();
    m_Layout = (m_Ui.GetVideoMode() == VIDEO_MODE_720P) ? &g_Layout720 : &g_Layout480;
    LoadThemeSetting();

    XDEVICE_PREALLOC_TYPE deviceTypes[] = {
        {XDEVICE_TYPE_GAMEPAD, 4},
    };
    XInitDevices(ARRAY_COUNT(deviceTypes), deviceTypes);

    // Give the controllers time to be enumerated
    Sleep(2000);

    static const DWORD ports[] = {XDEVICE_PORT0, XDEVICE_PORT1, XDEVICE_PORT2, XDEVICE_PORT3};
    for (int i = 0; i < ARRAY_COUNT(ports); i++)
    {
        m_Controllers[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, ports[i], XDEVICE_NO_SLOT, 0);
    }

    m_TotalHddSectors = get_hdd_size();
    // Needed before any table manipulator is created, it decides where the backup table is
    read_drive_identify();
    m_Physical4k = m_Drive.physical_4k;

    // Read kernel patch info + kernel reported partition table.
    // This also creates m_pTableOriginal, the copy used to check if partitions changed.
    // Without a patch m_pKernelTable stays NULL; RunStartupChecks() warns about it.
    reload_lba48();

    m_TableHdd = read_hdd_main_partition_table();
    m_TableHddBackup = read_hdd_backup_partition_table();

    RefreshClusterSizes();

    m_CurrentPart = m_pTable->GetLastAvailablePartitionSlot();

    FadeIn();
}

// Startup warnings, in one place and over the fully drawn main screen
void App::RunStartupChecks()
{
    // Cancel (the default) quits; QuitToDashboard() does not return
    if (m_pKernelTable == NULL && !DisplayNoInfo())
    {
        QuitToDashboard();
    }

    // Overview first if something in it needs checking; the specific warnings follow it
    if (SystemInfoNeedsAttention())
    {
        DisplaySystemInfo();
    }

    if (!IsMainTableUsable() && IsBackupTableUsable())
    {
        OfferBackupRestore();
    }

    CheckForV4TableOnOldBios();
    WarnBackupOverlapped();
    CheckTableStatus();
}

App::~App()
{
    delete m_pTableOriginal;
    delete m_pTable;

    m_Ui.Shutdown();
}

void App::MainLoop()
{
    RunStartupChecks();

    // One frame per pass (Present waits for the vertical blank), so the
    // selection bar slides smoothly; nothing in here sleeps
    while (true)
    {
        UpdateSelectionBar();

        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawControlsHint();
        m_Ui.EndFrame();

        // Nothing while a button from the screen just closed is still held
        if (ReadInput())
        {
            HandleMainScreenButtons(m_DigitalButtons, m_AnalogButton);
        }
    }
}

//-----------------------------------------------------------------------------
// Input handling
//-----------------------------------------------------------------------------

// The main screen only selects a partition and opens the editor or the menu.
// Changes happen in the edit page, writing, presets and loading tables in
// Start > Partition table, so a stray button press can't write anything.
// D-pad up/down: select (held: repeats), A: edit the selected partition
// (same as Start > Edit partition), Start: menu
void App::HandleMainScreenButtons(int buttons, int analog)
{
    const int direction = (buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;

    if (direction != 0)
    {
        const int move = m_MainScreenRepeat.Step(direction);
        const int part = m_CurrentPart + move;
        if (part >= 0 && part < XboxPartitionTableEntryCount)
        {
            m_CurrentPart = part;
        }
        return;
    }
    m_MainScreenRepeat.Step(0);

    if (analog == XINPUT_GAMEPAD_A)
    {
        // The edit page waits for A to be released before reading input
        DisplayEditPartition();
    }
    else if (buttons & XINPUT_GAMEPAD_START)
    {
        DisplayStartMenu();
    }
}

// Moves the main table's selection bar part of the way to the selected row,
// once per frame: it eases in and settles on the row
void App::UpdateSelectionBar()
{
    const FLOAT target = (FLOAT)TableRowTop(m_CurrentPart);

    if (!m_SelectionBarPlaced)
    {
        m_SelectionBarY = target;
        m_SelectionBarPlaced = true;
        return;
    }

    SlideTowards(m_SelectionBarY, target, m_SelectionBarTick);
}

// Back to the table as it was last written (or loaded at startup)
void App::UndoAllChanges()
{
    LoadTable(LOADED_TABLE_KERNEL);
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

//-----------------------------------------------------------------------------
// Screens
//-----------------------------------------------------------------------------

// Everything on the main screen except the bottom two lines (status and prompt)
void App::DrawMainScreen()
{
    m_Ui.DrawBackground();

    wchar_t title[100];
    swprintf(title, L"XBpartitioner 2.0 - LBA48 patch v%d", m_Lba48Version);
    m_Ui.Text(FONT_TEXT, title, m_Layout->title_x, m_Layout->title_y, COLOR_WHITE);

    DrawLoadedTableName();
    DrawDiskMap();
    DrawTable();
    DrawFreeSpace();
}

void App::DrawTable()
{
    // One panel like the menus (same colors), the rows sized from the table font
    const int bar_height = m_Ui.TextBoxHeight(FONT_TABLE, m_Layout->table_row_padding);
    const int last_slot = XboxPartitionTableEntryCount + 2;
    const int left = m_Layout->table_left;
    const int right = m_Layout->table_right;
    const int top = m_Layout->table_top;
    const int bottom = TableSlotTop(last_slot) + bar_height + (m_Layout->table_first_row_y - m_Layout->table_top);
    const int inset = m_Layout->menu_selection_inset;
    const int label_x = left + m_Layout->menu_text_inset;

    // The panel color with less opacity than menus and dialogs, so the moving
    // background shows through a little
    const D3DCOLOR panel = (Colors().panel & 0x00FFFFFF) | ((D3DCOLOR)TABLE_PANEL_ALPHA << 24);
    m_Ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, panel, panel);
    m_Ui.Outline((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel_border);

    // Column headers, the same width as the rows so the columns line up
    wchar_t row[80];
    const int header_top = TableSlotTop(0);
    FormatTableHeader(row);
    m_Ui.Text(FONT_TABLE, row, TableCenterX(), m_Ui.TextYCentered(FONT_TABLE, header_top, header_top + bar_height), COLOR_GREY, XFONT_CENTER);

    // Line between the headers and the rows, centered in the gap, like under a menu title
    const int line_y = (header_top + bar_height + TableSlotTop(1)) / 2;
    m_Ui.Rect((FLOAT)(left + inset), (FLOAT)line_y, (FLOAT)(right - inset), (FLOAT)(line_y + 1),
              Colors().panel_line, Colors().panel_line);

    // Selection bar at its sliding position (see UpdateSelectionBar), before any
    // row text so it never covers a row while moving between two of them.
    // Screens drawn outside the main loop (menus, dialogs) don't animate it,
    // so before the first frame it sits on the row.
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

        // Selected row in yellow (also when disabled), disabled ones grey
        Color text_color = m_pTable->isPartitionEnabled(i) ? COLOR_WHITE : COLOR_GREY;
        if (m_CurrentPart == i)
        {
            text_color = COLOR_YELLOW;
        }

        // Rows are digits and capitals only, so centering on the capitals is exact
        m_Ui.Text(FONT_TABLE, row, TableCenterX(), m_Ui.TextYCentered(FONT_TABLE, bar_top, bar_top + bar_height), text_color, XFONT_CENTER);
    }
}

// Color of the k-th of count partitions on the disk map, left to right: along
// the way from the theme's first map color to its second, every other one a
// bit darker, so neighbours always differ but all fit the theme
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

// The partitions to scale across the space they can use, above the table: free
// space dark, each partition its own color fitting the theme (disk_map_color),
// the selected one in the theme's highlight color. The map runs from the first
// usable sector to the last one (before the backup table), and a partition that
// ends less than 1 MB from there is drawn to the right edge, so a disk that is
// fully used shows a full bar.
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
    // Gaps at the end smaller than this don't show (alignment, the backup table)
    static const ULONGLONG MAP_END_SNAP = 0x800; // 1 MB

    // Inside the border
    const FLOAT inner_left = left + 1.0f;
    const FLOAT inner_right = right - 1.0f;
    const double scale = (double)(inner_right - inner_left) / (double)(map_end - map_start);

    // Each partition's place from the left (by start), for its color
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

            // Clamped to the map (tables from other tools can go past the end)
            const ULONGLONG part_start = (start < map_start) ? map_start : (start > map_end) ? map_end : start;
            const ULONGLONG part_end = (start + size > map_end) ? map_end : start + size;
            const bool at_end = (part_end + MAP_END_SNAP >= map_end);

            FLOAT x1 = inner_left + (FLOAT)((double)(part_start - map_start) * scale);
            FLOAT x2 = at_end ? inner_right : inner_left + (FLOAT)((double)(part_end - map_start) * scale);
            // A pixel at least
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

// A start or end sector, 48 bits as 12 hex digits (also used by the editor)
static void format_table_lba(ULONGLONG lba, wchar_t *out)
{
    swprintf(out, L"%.04X%.08X", (ULONG)(WORD)(lba >> 32), (ULONG)(lba & LBA_LOWER_BITS));
}

// Table rows and the header are fixed width strings (52 characters), so
// centering keeps the columns lined up:
//   "Part  Start         End                Size  FATX    "
//   " 6 F  000000EE8AB0  0000E8E08000  931.51 GB  64KB  *"
//   number and drive letter (4), start and end (48 bit LBAs, 12 hex digits),
//   size (9, right aligned), cluster size on disk (5, blank if unformatted),
//   and a marker compared with the saved table: * changed, + new, - removed.
// A slot that is not used (now or in the saved table) just says so.
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

    // Compared with the saved table, like the editor's status
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

    // Cluster size of the FATX on disk; ERROR if too small for the partition
    // size (same rule as the formatter), blank if unformatted or empty
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
    // Free: not used by an enabled partition. Usable: what partitions can use
    // (after the stock start, before the backup table).
    const uint64_t usable_lbas = m_pTable->GetUsableLBAs();
    const uint64_t free_lbas = m_pTable->GetAvailableLBAs();

    wchar_t free_size[32];
    wchar_t usable_size[32];
    format_human_size(free_lbas, free_size);
    format_human_size(usable_lbas, usable_size);

    wchar_t size_line[100];
    swprintf(size_line, L"Free: %s of %s", free_size, usable_size);
    m_Ui.Text(FONT_TEXT, size_line, m_Layout->margin_x, m_Layout->free_space_y, COLOR_WHITE);

    // What the markers in the table's last column mean, right under the table
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

// Dialog box over the main screen: a title, message lines and buttons.
// With a cancel_label there are two buttons, OK and Cancel, and Cancel is
// selected to start with. Left/Right switch, A presses the selected one, and
// B, Back or Start cancel. Returns true for OK.
// Without a cancel_label there is only OK (for messages): any of those buttons
// closes it, and it returns true.
// title_color: COLOR_WARNING for warnings, COLOR_WHITE for plain questions.
bool App::RunDialog(LPCWSTR title, Color title_color, const LPCWSTR *lines, int line_count, LPCWSTR ok_label, LPCWSTR cancel_label)
{
    const bool has_cancel = (cancel_label != NULL);
    bool ok_selected = !has_cancel; // Cancel is the default

    // Buttons are sized around their label
    const int button_height = m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->dialog_button_padding);
    const int height = m_Layout->dialog_title_height + line_count * m_Layout->dialog_line_height + m_Layout->dialog_padding + button_height + m_Layout->dialog_padding;
    const int top = (m_Layout->height - height) / 2;
    const int buttons_y = top + m_Layout->dialog_title_height + line_count * m_Layout->dialog_line_height + m_Layout->dialog_padding;

    // OK on the left, Cancel on the right; a single OK is centered
    const int buttons_width = has_cancel ? 2 * m_Layout->dialog_button_width + m_Layout->dialog_button_gap : m_Layout->dialog_button_width;
    const int ok_x = (m_Layout->width - buttons_width) / 2;
    const int cancel_x = ok_x + m_Layout->dialog_button_width + m_Layout->dialog_button_gap;

    // The button that led here may still be held
    m_WaitForRelease = true;

    while (true)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();

        // Same box style as the menus: border, and a line under the title
        m_Ui.Rect((FLOAT)m_Layout->dialog_left, (FLOAT)top, (FLOAT)m_Layout->dialog_right, (FLOAT)(top + height), Colors().panel, Colors().panel);
        m_Ui.Outline((FLOAT)m_Layout->dialog_left, (FLOAT)top, (FLOAT)m_Layout->dialog_right, (FLOAT)(top + height), Colors().panel_border);
        m_Ui.Text(FONT_TITLE, title, m_Layout->width / 2, top + m_Layout->dialog_title_offset, title_color, XFONT_CENTER);

        // Halfway between the bottom of the title text and the first message line
        const int title_bottom = top + m_Layout->dialog_title_offset + m_Ui.FontHeight(FONT_TITLE);
        const int header_line_y = (title_bottom + top + m_Layout->dialog_title_height) / 2;
        m_Ui.Rect((FLOAT)(m_Layout->dialog_left + m_Layout->menu_selection_inset), (FLOAT)header_line_y,
                  (FLOAT)(m_Layout->dialog_right - m_Layout->menu_selection_inset), (FLOAT)(header_line_y + 1),
                  Colors().panel_line, Colors().panel_line);

        for (int i = 0; i < line_count; i++)
        {
            m_Ui.Text(FONT_TEXT, lines[i], (m_Layout->dialog_left + m_Layout->dialog_text_inset), top + m_Layout->dialog_title_height + i * m_Layout->dialog_line_height, COLOR_LIGHT_GREY);
        }

        for (int button = 0; button < (has_cancel ? 2 : 1); button++)
        {
            const bool is_ok = (button == 0);
            const int x = is_ok ? ok_x : cancel_x;
            const bool selected = (is_ok == ok_selected);
            const D3DCOLOR color = selected ? Colors().selection : Colors().button;

            m_Ui.Rect((FLOAT)x, (FLOAT)buttons_y, (FLOAT)(x + m_Layout->dialog_button_width), (FLOAT)(buttons_y + button_height), color, color);
            m_Ui.Text(FONT_TEXT, is_ok ? ok_label : cancel_label, x + m_Layout->dialog_button_width / 2,
                      m_Ui.TextYCentered(FONT_TEXT, buttons_y, buttons_y + button_height), selected ? COLOR_YELLOW : COLOR_WHITE, XFONT_CENTER);
        }

        m_Ui.EndFrame();

        // Polled every frame so the screen keeps moving; a press counts once
        if (!ReadInput() || (m_AnalogButton < 0 && m_DigitalButtons == 0))
        {
            continue;
        }
        m_WaitForRelease = true;

        const bool cancel = (m_AnalogButton == XINPUT_GAMEPAD_B) ||
                            (m_DigitalButtons & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) != 0;

        if (!has_cancel)
        {
            if (cancel || m_AnalogButton == XINPUT_GAMEPAD_A)
            {
                return true;
            }
            continue;
        }

        if (cancel)
        {
            return false;
        }

        if (m_DigitalButtons & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT))
        {
            ok_selected = !ok_selected;
            continue;
        }

        if (m_AnalogButton == XINPUT_GAMEPAD_A)
        {
            return ok_selected;
        }
    }
}

// Warning with OK and Cancel (Cancel selected). True for OK.
bool App::ConfirmWarning(const LPCWSTR *lines, int line_count, LPCWSTR ok_label)
{
    return RunDialog(L"Warning", COLOR_WARNING, lines, line_count, ok_label, L"Cancel");
}

// Warning with just OK, for things the user only needs to know
void App::ShowWarning(const LPCWSTR *lines, int line_count)
{
    RunDialog(L"Warning", COLOR_WARNING, lines, line_count, L"OK", NULL);
}

void App::FadeIn()
{
    for (int alpha = 0xff; alpha > 0x06; alpha -= 0x06)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawControlsHint();
        m_Ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_Ui.EndFrame();
    }
}

// The main screen fading to black, the background still moving
void App::FadeOut()
{
    for (int alpha = 0x06; alpha < 0xff; alpha += 0x06)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        m_Ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_Ui.EndFrame();
    }
    m_Ui.BeginFrame();
    m_Ui.FillScreen(D3DCOLOR_ARGB(0xff, 0, 0, 0));
    m_Ui.EndFrame();
}

// Main screen with a progress message in the status line ("Formatting...").
// Does not wait: results that need a button go through ShowMessage().
void App::ShowStatus(LPCWSTR message, Color color)
{
    m_Ui.BeginFrame();
    DrawMainScreen();
    m_Ui.Text(FONT_TEXT, message, m_Layout->margin_x, m_Layout->status_y, color);
    m_Ui.EndFrame();
}

// Result or error message: a dialog with a single OK
void App::ShowMessage(LPCWSTR title, Color title_color, LPCWSTR message)
{
    RunDialog(title, title_color, &message, 1, L"OK", NULL);
}

// "931.51 GB (1953525168 sectors)"
static void format_disk_size(ULONGLONG sectors, wchar_t *out)
{
    wchar_t size[32];
    format_human_size(sectors, size);
    swprintf(out, L"%s (%I64u sectors)", size, sectors);
}

// True if the system information screen shows something the user should look
// at, and no other startup warning already covers it:
//   - the drive could not be identified
//   - the BIOS reports less than the drive's own size
//   - there is no usable partition table at all (main and backup both missing
//     or damaged; a usable backup gets the restore question instead)
//   - the backup is missing, or only found at the old location
// No lba48 patch and main/backup mismatches have their own warnings too.
bool App::SystemInfoNeedsAttention()
{
    if (!m_Drive.identify_ok)
    {
        return true;
    }

    // Without a patch the size is always the 28 bit one, DisplayNoInfo() covers that
    if (m_pKernelTable != NULL && m_TotalHddSectors < m_Drive.native_sectors)
    {
        return true;
    }

    if (!IsMainTableUsable() && !IsBackupTableUsable())
    {
        return true;
    }

    return m_BackupStatus == BACKUP_MISSING || m_BackupStatus == BACKUP_FOUND_LEGACY;
}

// Drive, BIOS and partition table overview. Shown at startup when
// SystemInfoNeedsAttention(), and from the Start menu.
void App::DisplaySystemInfo()
{
    // Rows are collected first, so the panel can be sized to them
    struct InfoRow
    {
        LPCWSTR label;
        wchar_t value[80];
        Color color;
    };
    InfoRow rows[12];
    int row_count = 0;

#define ADD_INFO_ROW(row_label, row_color) \
    rows[row_count].label = (row_label); \
    rows[row_count].color = (row_color); \
    row_count++

    // Drive, as reported by the drive itself
    if (m_Drive.identify_ok)
    {
        swprintf(rows[row_count].value, L"%s", m_Drive.model[0] ? m_Drive.model : L"(empty)");
        ADD_INFO_ROW(L"HDD model", COLOR_WHITE);

        swprintf(rows[row_count].value, L"%s", m_Drive.firmware[0] ? m_Drive.firmware : L"(empty)");
        ADD_INFO_ROW(L"Firmware", COLOR_WHITE);

        format_disk_size(m_Drive.native_sectors, rows[row_count].value);
        ADD_INFO_ROW(L"Drive size", COLOR_WHITE);

        if (m_Drive.physical_sector_size > HDD_SECTOR_SIZE)
        {
            swprintf(rows[row_count].value, L"%u logical, %u physical (512e)", HDD_SECTOR_SIZE, m_Drive.physical_sector_size);
        }
        else
        {
            swprintf(rows[row_count].value, L"%u bytes", HDD_SECTOR_SIZE);
        }
        ADD_INFO_ROW(L"Sector size", COLOR_WHITE);
    }
    else
    {
        swprintf(rows[row_count].value, L"no drive info (ATA IDENTIFY failed)");
        ADD_INFO_ROW(L"HDD", COLOR_YELLOW);
    }

    // What the BIOS/kernel makes of it
    const bool size_capped = m_Drive.identify_ok && m_TotalHddSectors < m_Drive.native_sectors;
    format_disk_size(m_TotalHddSectors, rows[row_count].value);
    ADD_INFO_ROW(L"BIOS size", size_capped ? COLOR_YELLOW : COLOR_WHITE);
    if (size_capped)
    {
        swprintf(rows[row_count].value, L"(less than the drive: BIOS limit)");
        ADD_INFO_ROW(L"", COLOR_YELLOW);
    }

    if (m_pKernelTable == NULL)
    {
        swprintf(rows[row_count].value, L"none (stock kernel, max 128GB)");
        ADD_INFO_ROW(L"LBA48 patch", COLOR_RED);
    }
    else if (m_FullLba48)
    {
        swprintf(rows[row_count].value, L"v%u (48 bit LBAs)", m_Lba48Version);
        ADD_INFO_ROW(L"LBA48 patch", COLOR_GREEN);
    }
    else
    {
        swprintf(rows[row_count].value, L"v%u (32 bit LBAs, max 2TB)", m_Lba48Version);
        ADD_INFO_ROW(L"LBA48 patch", COLOR_YELLOW);
    }

    format_disk_size(m_pTable->GetUsableLBAs(), rows[row_count].value);
    ADD_INFO_ROW(L"Usable", COLOR_WHITE);

    // Partition tables
    const bool main_valid = IsMainTableUsable();
    if (main_valid)
    {
        swprintf(rows[row_count].value, L"valid (sector 0)");
        ADD_INFO_ROW(L"Partition table", COLOR_GREEN);
    }
    else
    {
        swprintf(rows[row_count].value, L"%s", XboxPartitionTableManipulator::HasMagic(m_TableHdd) ? L"damaged (sector 0)" : L"missing");
        ADD_INFO_ROW(L"Partition table", COLOR_RED);
    }

    wchar_t *value = rows[row_count].value;
    Color backup_color = COLOR_YELLOW;
    switch (m_BackupStatus)
    {
    case BACKUP_FOUND:
        if (!IsBackupTableUsable())
        {
            swprintf(value, L"damaged (sector %I64u)", m_pTable->GetBackupTableLba());
            backup_color = COLOR_RED;
        }
        else if (!main_valid || memcmp(&m_TableHdd, &m_TableHddBackup, sizeof(XboxPartitionTable)) == 0)
        {
            swprintf(value, L"valid (sector %I64u)", m_pTable->GetBackupTableLba());
            backup_color = main_valid ? COLOR_GREEN : COLOR_YELLOW;
        }
        else
        {
            swprintf(value, L"differs from main! (sector %I64u)", m_pTable->GetBackupTableLba());
        }
        break;
    case BACKUP_FOUND_LEGACY:
        if (IsBackupTableUsable())
        {
            swprintf(value, L"valid, old location (sector %I64u)", m_pTable->GetLastSectorLba());
        }
        else
        {
            swprintf(value, L"damaged, old location (sector %I64u)", m_pTable->GetLastSectorLba());
            backup_color = COLOR_RED;
        }
        break;
    case BACKUP_MISSING:
        swprintf(value, L"missing (sector %I64u)", m_pTable->GetBackupTableLba());
        break;
    case BACKUP_OVERLAPPED:
        swprintf(value, L"none, last partition covers it");
        break;
    default:
        if (m_pKernelTable == NULL)
        {
            swprintf(value, L"not used (no LBA48 patch)");
        }
        else
        {
            swprintf(value, L"none (no room after stock partitions)");
        }
        backup_color = COLOR_GREY;
        break;
    }
    ADD_INFO_ROW(L"Backup table", backup_color);

    swprintf(rows[row_count].value, L"%s", m_Ui.GetVideoModeName());
    ADD_INFO_ROW(L"Video mode", COLOR_WHITE);

#undef ADD_INFO_ROW

    // An expanded menu: same panel, rows spaced like menu items, centered on screen
    const int bar_height = MenuItemBarHeight();
    const int item_height = MenuItemHeight();
    const int height = m_Layout->menu_first_item_offset + row_count * item_height + m_Layout->menu_footer_height;
    const int top = (m_Layout->height - height) / 2;
    const int bottom = top + height;
    const int first_item_y = top + m_Layout->menu_first_item_offset;
    const int label_x = m_Layout->info_left + m_Layout->menu_text_inset;
    const int value_x = m_Layout->info_left + m_Layout->info_value_offset;

    // Drawn every frame until a button is pressed; the one that opened it may
    // still be held
    m_WaitForRelease = true;

    while (true)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawPanel(m_Layout->info_left, top, m_Layout->info_right, bottom, L"System information", first_item_y);

        for (int i = 0; i < row_count; i++)
        {
            const int bar_top = first_item_y + i * item_height;
            const int text_y = m_Ui.TextYCentered(FONT_TEXT, bar_top, bar_top + bar_height);
            m_Ui.Text(FONT_TEXT, rows[i].label, label_x, text_y, COLOR_GREY);
            m_Ui.Text(FONT_TEXT, rows[i].value, value_x, text_y, rows[i].color);
        }

        DrawPanelFooter(m_Layout->info_left, m_Layout->info_right, bottom, L"Press any button to close");
        m_Ui.EndFrame();

        if (ReadInput() && (m_AnalogButton >= 0 || m_DigitalButtons != 0))
        {
            m_WaitForRelease = true;
            return;
        }
    }
}

// Disk size to check partition ends against: without an lba48 patch the reported
// size is the 28 bit one (~128GB), so a good table for a bigger drive would look
// damaged; 0 skips that check.
static ULONGLONG table_check_size(const XboxPartitionTable *kernel_table, ULONGLONG total_sectors)
{
    return (kernel_table != NULL) ? total_sectors : 0;
}

bool App::IsMainTableUsable()
{
    return XboxPartitionTableManipulator::IsTableUsable(m_TableHdd, table_check_size(m_pKernelTable, m_TotalHddSectors));
}

bool App::IsBackupTableUsable()
{
    return XboxPartitionTableManipulator::IsTableUsable(m_TableHddBackup, table_check_size(m_pKernelTable, m_TotalHddSectors));
}

// Main table missing or damaged, backup fine: ask to write the backup back as
// the main table. No (the default) leaves the disk alone; the backup can still
// be loaded later from Partition table > Load partition table.
void App::OfferBackupRestore()
{
    const LPCWSTR lines[] = {
        XboxPartitionTableManipulator::HasMagic(m_TableHdd) ? L"The main partition table is damaged." : L"The main partition table is missing.",
        L"The backup partition table is valid.",
        L"Restore it? It is written as the main table.",
    };
    if (!RunDialog(L"Warning", COLOR_WARNING, lines, ARRAY_COUNT(lines), L"Restore", L"No"))
    {
        return;
    }

    // Writes it to sector 0 and the backup sector, and gives it to the kernel,
    // so the partitions work right away; the editor reloads from it too
    if (write_partition_tables(m_TableHddBackup))
    {
        ShowMessage(L"Done", COLOR_GREEN, L"The backup partition table was restored.");
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The backup partition table could not be restored.");
    }
}

// A pre v4 lba48 patch only uses the low 32 bits of each LBA. If a table has
// partitions that need the v4 high bits (over 2TB), it was most likely made
// with a v4 BIOS, and this BIOS sees those partitions in the wrong place.
// The editor drops the high bits (see XboxPartitionTableManipulator), so
// warn before the user writes or formats anything based on it.
void App::CheckForV4TableOnOldBios()
{
    if (m_FullLba48 || m_pKernelTable == NULL)
    {
        // v4 handles it, or there is no lba48 patch at all (already warned)
        return;
    }

    // Which tables are affected, e.g. "kernel, hdd"
    wchar_t found_in[40] = L"";
    const struct
    {
        const XboxPartitionTable *table;
        LPCWSTR name;
    } tables[] = {
        {&m_TableKernel, L"kernel"},
        {&m_TableHdd, L"hdd"},
        {&m_TableHddBackup, L"hdd backup"},
    };

    for (int i = 0; i < ARRAY_COUNT(tables); i++)
    {
        if (XboxPartitionTableManipulator::HasMagic(*tables[i].table) &&
            XboxPartitionTableManipulator::HasHighLbaBits(*tables[i].table))
        {
            if (found_in[0] != L'\0')
            {
                wcscat(found_in, L", ");
            }
            wcscat(found_in, tables[i].name);
        }
    }

    if (found_in[0] == L'\0')
    {
        return;
    }

    wchar_t line1[80];
    wchar_t line3[80];
    swprintf(line1, L"This BIOS has LBA48 patch v%d, but a partition table", m_Lba48Version);
    swprintf(line3, L"Found in: %s.", found_in);

    const LPCWSTR lines[] = {
        line1,
        L"uses v4 values (over 2TB), likely from a v4 BIOS.",
        line3,
        L"Partitions over 2TB are shown in the wrong place.",
        L"Do not write or format. Use a v4 BIOS such as Cerbios.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

// Lists what will be formatted, calls out E and C, returns true on Y
bool App::ConfirmFormatWarning(bool format_changed)
{
    // Which partitions get formatted: the changed ones, or the selected one
    bool marked[XboxPartitionTableEntryCount];
    if (format_changed)
    {
        GetChangedPartitions(marked, false);
    }
    else
    {
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            marked[i] = (i == m_CurrentPart);
        }
    }

    wchar_t list[100];
    FormatPartitionList(marked, list, ARRAY_COUNT(list));

    wchar_t message[120];
    if (list[0] == L'\0')
    {
        swprintf(message, L"No partitions changed, nothing will be formatted.");
    }
    else
    {
        swprintf(message, L"Format: %s", list);
    }

    // Index 0 is E, 1 is C (see partStr)
    LPCWSTR lines[4];
    int line_count = 0;
    wchar_t program_line[100];
    lines[line_count++] = message;
    if (marked[0])
    {
        lines[line_count++] = L"E holds saves and settings!";
    }
    if (marked[1])
    {
        lines[line_count++] = L"C holds the dashboard, the Xbox may not boot!";
    }
    if (m_LaunchPartition >= 0 && marked[m_LaunchPartition])
    {
        FormatLaunchPartitionLine(program_line, L"it will be deleted.");
        lines[line_count++] = program_line;
    }
    else if (format_changed && MovesLaunchPartition(m_pTable->GetPartitionTable()))
    {
        // Removed in the new table, so not formatted, but gone all the same
        FormatLaunchPartitionLine(program_line, L"it will be unreadable.");
        lines[line_count++] = program_line;
    }

    return ConfirmWarning(lines, line_count, L"Format");
}

// Returns true if the user chose to continue without lba48 info
bool App::DisplayNoInfo()
{
    static const LPCWSTR lines[] = {
        L"No LBA48 patch found: custom partitions will not work",
        L"with this BIOS. Update your BIOS to Cerbios.",
    };
    // Cancel (the default) quits
    return RunDialog(L"Warning", COLOR_WARNING, lines, ARRAY_COUNT(lines), L"Continue", L"Quit");
}

void App::CheckTableStatus()
{
    // Check if we should report that main and backup dont match up.
    // Only when both are usable: a missing or damaged main gets the restore
    // question (OfferBackupRestore), and a missing backup is normal for drives
    // partitioned with other tools.
    if (IsMainTableUsable() && IsBackupTableUsable() &&
        memcmp(&m_TableHdd, &m_TableHddBackup, sizeof(XboxPartitionTable)) != 0)
    {
        static const LPCWSTR lines[] = {
            L"The main and backup partition tables don't match!",
            L"Check which one is correct before doing anything.",
            L"Compare them via Partition table > Load partition table.",
        };
        ShowWarning(lines, ARRAY_COUNT(lines));
    }
}

// Asks before exiting: a warning with unsaved table changes, otherwise a
// plain question. True for Exit.
bool App::ConfirmExit()
{
    if (CheckIfPartitionTableChanged())
    {
        static const LPCWSTR lines[] = {
            L"The partition table has changes that were not written.",
            L"They will be lost when you exit.",
        };
        return ConfirmWarning(lines, ARRAY_COUNT(lines), L"Exit");
    }

    static const LPCWSTR lines[] = {
        L"Exit XBpartitioner and return to the dashboard?",
    };
    return RunDialog(L"Exit", COLOR_WHITE, lines, ARRAY_COUNT(lines), L"Exit", L"Cancel");
}

// Fades the current screen to black and returns to the dashboard. Does not return.
void App::QuitToDashboard()
{
    FadeOut();
    m_Ui.Shutdown();

    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};
    XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
}

//-----------------------------------------------------------------------------
// Actions bound to Start
//-----------------------------------------------------------------------------

void App::DoFormatAll()
{
    if (!ConfirmFormatWarning(true))
    {
        return;
    }

    XboxPartitionTable table = m_pTable->GetPartitionTable();

    // write_partition_tables() makes the new table the baseline, so work out
    // which partitions changed first. Same list the warning showed.
    bool to_format[XboxPartitionTableEntryCount];
    GetChangedPartitions(to_format, false);

    bool backup_written = false;
    if (!write_partition_tables(table, &backup_written))
    {
        ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
        return;
    }

    // Without the lba48 patch the kernel keeps using its old partition layout
    // until a reboot, so formatting now would hit the wrong sectors
    if (!m_pKernelTable)
    {
        ShowMessage(L"Not formatted", COLOR_YELLOW, L"Table written. Formatting needs an LBA48 BIOS.");
        return;
    }

    ShowStatus(L"Partition table written...", COLOR_GREEN);
    Sleep(500);

    int attempted = 0;
    int formatted = 0;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (to_format[i])
        {
            wchar_t format_str[50];
            swprintf(format_str, L"Formatting partition %d with %dk clusters...", i + 1, m_pTable->CalculateClusterSizeForPartition(i));
            ShowStatus(format_str, COLOR_GREEN);

            attempted++;
            if (format_partition(i))
            {
                formatted++;
            }
        }
    }

    // Failures were already shown one by one; this says it is finished
    wchar_t done[60];
    swprintf(done, L"Table written, %d of %d partitions formatted.", formatted, attempted);
    const LPCWSTR lines[] = {
        done,
        L"No backup: the last partition covers its sector.",
    };
    const bool ok = (formatted == attempted);
    RunDialog(ok ? L"Done" : L"Finished with errors", ok ? COLOR_GREEN : COLOR_YELLOW,
              lines, (backup_written || m_pTable->GetBackupTableLba() == 0 || m_pKernelTable == NULL) ? 1 : 2, L"OK", NULL);
}

// At startup: the last partition on disk covers the backup table sector, so no
// backup is read or written until a table that leaves room for it is written
void App::WarnBackupOverlapped()
{
    if (m_BackupStatus != BACKUP_OVERLAPPED)
    {
        return;
    }

    static const LPCWSTR lines[] = {
        L"The last partition covers the backup table sector",
        L"(on 4K sector drives it is in the last 4K block).",
        L"No backup is written until the layout leaves room:",
        L"shrink the last partition, or use a new partition table.",
    };
    ShowWarning(lines, ARRAY_COUNT(lines));
}

void App::DoWriteTable()
{
    // FATX works out its layout from the partition size, so a partition that
    // already existed and was moved or resized is unreadable until formatted
    bool resized[XboxPartitionTableEntryCount];
    const int resized_count = GetChangedPartitions(resized, true);
    const bool moves_program = MovesLaunchPartition(m_pTable->GetPartitionTable());
    if (resized_count > 0 || moves_program)
    {
        wchar_t list[100];
        FormatPartitionList(resized, list, ARRAY_COUNT(list));

        wchar_t line1[120];
        swprintf(line1, L"Moved or resized: %s", list);
        wchar_t program_line[100];
        FormatLaunchPartitionLine(program_line, L"it will be unreadable.");

        LPCWSTR lines[4];
        int line_count = 0;
        if (resized_count > 0)
        {
            lines[line_count++] = line1;
            lines[line_count++] = L"Their data is unreadable until they are formatted.";
            lines[line_count++] = L"Use Start > Partition table > Write table and format.";
        }
        if (moves_program)
        {
            lines[line_count++] = program_line;
        }
        if (!ConfirmWarning(lines, line_count, L"Write anyway"))
        {
            return;
        }
    }
    else
    {
        static const LPCWSTR lines[] = {
            L"The partition table will be written to the hard drive.",
        };
        if (!RunDialog(L"Write table", COLOR_WHITE, lines, ARRAY_COUNT(lines), L"Write", L"Cancel"))
        {
            return;
        }
    }

    bool backup_written = false;
    if (write_partition_tables(m_pTable->GetPartitionTable(), &backup_written))
    {
        LPCWSTR result = L"The partition table was written.";
        if (m_pKernelTable == NULL)
        {
            result = L"Table written (no backup without an LBA48 BIOS).";
        }
        else if (!backup_written && m_pTable->GetBackupTableLba() != 0)
        {
            result = L"Table written, no backup: the last partition covers it.";
        }
        ShowMessage(L"Done", COLOR_GREEN, result);
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The partition table could not be written.");
    }
}

void App::DoFormatOne()
{
    // The partition is formatted where the kernel's table puts it. Formatting an
    // edited, unwritten layout would mean pushing it into the kernel for this
    // boot only: after a reboot the old table on disk is back and anything
    // stored meanwhile is in the wrong place. So the table must be saved first.
    if (CheckIfPartitionTableChanged())
    {
        ShowMessage(L"Not formatted", COLOR_YELLOW, L"Unsaved changes: write the table first.");
        return;
    }

    if (!ConfirmFormatWarning(false))
    {
        return;
    }

    wchar_t format_str[50];
    swprintf(format_str, L"Formatting partition %d with %dk clusters...", m_CurrentPart + 1, m_pTable->CalculateClusterSizeForPartition(m_CurrentPart));
    ShowStatus(format_str, COLOR_GREEN);

    // Failures show their own message
    if (format_partition(m_CurrentPart))
    {
        wchar_t done[40];
        swprintf(done, L"Partition %d was formatted.", m_CurrentPart + 1);
        ShowMessage(L"Done", COLOR_GREEN, done);
    }
}

// Menu style box (menus, system information): background, white border, the
// title, and a line between the title and the first item, halfway between the
// bottom of the title text and first_item_y, as wide as the selection bars.
// Colors come from the theme (panel, panel_border, panel_line).
void App::DrawPanel(int left, int top, int right, int bottom, LPCWSTR title, int first_item_y)
{
    m_Ui.Rect((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel, Colors().panel);
    m_Ui.Outline((FLOAT)left, (FLOAT)top, (FLOAT)right, (FLOAT)bottom, Colors().panel_border);
    m_Ui.Text(FONT_TEXT, title, (left + right) / 2, top + m_Layout->menu_title_offset, COLOR_WHITE, XFONT_CENTER);

    const int title_bottom = top + m_Layout->menu_title_offset + m_Ui.FontHeight(FONT_TEXT);
    const int header_line_y = (title_bottom + first_item_y) / 2;
    m_Ui.Rect((FLOAT)(left + m_Layout->menu_selection_inset), (FLOAT)header_line_y,
              (FLOAT)(right - m_Layout->menu_selection_inset), (FLOAT)(header_line_y + 1),
              Colors().panel_line, Colors().panel_line);
}

// Grey hint at the bottom of a panel (the last menu_footer_height pixels)
void App::DrawPanelFooter(int left, int right, int bottom, LPCWSTR text, Color color)
{
    m_Ui.Text(FONT_TEXT, text, (left + right) / 2, bottom - m_Layout->menu_footer_height + m_Layout->menu_footer_text_offset, color, XFONT_CENTER);
}

// Menu box over the main screen. D-pad up/down moves (skipping disabled items,
// held: repeats), A selects, B goes back one level (MENU_RESULT_BACK), Start
// closes all menus (MENU_RESULT_CLOSE). Returns the selected index otherwise.
// Runs one frame per pass so the selection bar slides between items.
// initial: item to start on, e.g. the one used last time; submenu: footer says
// "B: back" instead of "B: close".
// Menus with more than MENU_MAX_VISIBLE_ITEMS items (the theme list) show that
// many and scroll to keep the selection in view, with a scroll bar on the right.
static const int MENU_MAX_VISIBLE_ITEMS = 8;

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

// Top of the scroll bar's thumb for the first item shown
static FLOAT menu_thumb_top(int track_top, int track_height, int thumb_height, int first_visible, int count, int visible)
{
    if (count <= visible)
    {
        return (FLOAT)track_top;
    }
    return (FLOAT)track_top + (FLOAT)((track_height - thumb_height) * first_visible) / (FLOAT)(count - visible);
}

int App::RunMenu(LPCWSTR title, const LPCWSTR *labels, const bool *enabled, int count, int initial, bool submenu,
                 MenuPreview preview)
{
    int selected = (initial >= 0 && initial < count) ? initial : 0;
    for (int tries = 0; tries < count && !enabled[selected]; tries++)
    {
        selected = (selected + 1) % count;
    }
    if (!enabled[selected])
    {
        return MENU_RESULT_BACK;
    }

    const FLOAT box_left = (FLOAT)m_Layout->menu_left;
    const FLOAT box_right = (FLOAT)m_Layout->menu_right;
    // Each item's selection bar is sized around its text, items are a bar plus a gap apart
    const int bar_height = MenuItemBarHeight();
    const int item_height = MenuItemHeight();
    const int visible = (count > MENU_MAX_VISIBLE_ITEMS) ? MENU_MAX_VISIBLE_ITEMS : count;
    const bool scrolls = (visible < count);
    const int box_top = m_Layout->menu_top;
    const int box_height = m_Layout->menu_first_item_offset + visible * item_height + m_Layout->menu_footer_height;
    const int first_item_y = box_top + m_Layout->menu_first_item_offset;
    const int box_bottom = box_top + box_height;
    int previewed = -1;

    // Scrolling: the first item shown, and the scroll bar on the right of the
    // items (the selection bars stop short of it). The thumb's length is the
    // part of the list shown.
    int first_visible = 0;
    const int scroll_width = (m_Layout->menu_selection_inset + 1) / 2;
    const FLOAT scroll_right = box_right - m_Layout->menu_selection_inset;
    const FLOAT scroll_left = scroll_right - scroll_width;
    const FLOAT selection_right = scrolls ? scroll_left - m_Layout->menu_selection_inset / 2 : scroll_right;
    const int track_top = first_item_y;
    const int track_height = visible * item_height - m_Layout->menu_item_gap;
    const int thumb_height = scrolls ? (track_height * visible + count / 2) / count : track_height;

    first_visible = menu_scroll_to(selected, first_visible, visible);

    // Start on the selected item, then slide towards it whenever it changes;
    // the thumb slides the same way
    FLOAT bar_y = (FLOAT)(first_item_y + (selected - first_visible) * item_height);
    FLOAT thumb_y = menu_thumb_top(track_top, track_height, thumb_height, first_visible, count, visible);
    DWORD slide_tick = 0;
    DWORD thumb_tick = 0;
    DpadRepeat repeat;

    // The button that opened the menu (Start, A) is still held
    m_WaitForRelease = true;

    while (true)
    {
        if (preview != NULL && previewed != selected)
        {
            (this->*preview)(selected);
            previewed = selected;
        }

        first_visible = menu_scroll_to(selected, first_visible, visible);
        SlideTowards(bar_y, (FLOAT)(first_item_y + (selected - first_visible) * item_height), slide_tick);
        SlideTowards(thumb_y, menu_thumb_top(track_top, track_height, thumb_height, first_visible, count, visible), thumb_tick);

        m_Ui.BeginFrame();
        DrawMainScreen();

        DrawPanel(m_Layout->menu_left, box_top, m_Layout->menu_right, box_bottom, title, first_item_y);

        // Selection bar at its sliding position, before the item text so it
        // never covers an item while moving between two of them
        const int selection_top = (int)(bar_y + 0.5f);
        m_Ui.Rect(box_left + m_Layout->menu_selection_inset, (FLOAT)selection_top,
                  selection_right, (FLOAT)(selection_top + bar_height),
                  Colors().selection, Colors().selection);

        if (scrolls)
        {
            // Track, then the thumb at its sliding position
            const int thumb_top = (int)(thumb_y + 0.5f);
            m_Ui.Rect(scroll_left, (FLOAT)track_top, scroll_right, (FLOAT)(track_top + track_height),
                      Colors().button, Colors().button);
            m_Ui.Rect(scroll_left, (FLOAT)thumb_top, scroll_right, (FLOAT)(thumb_top + thumb_height),
                      Colors().panel_border, Colors().panel_border);
        }

        for (int i = first_visible; i < first_visible + visible; i++)
        {
            const int bar_top = first_item_y + (i - first_visible) * item_height;
            const int bar_bottom = bar_top + bar_height;
            Color color = enabled[i] ? COLOR_WHITE : COLOR_GREY;

            if (i == selected)
            {
                color = COLOR_YELLOW;
            }

            // Centered on the capital letters (see TextYCentered)
            m_Ui.Text(FONT_TEXT, labels[i], m_Layout->menu_left + m_Layout->menu_text_inset,
                      m_Ui.TextYCentered(FONT_TEXT, bar_top, bar_bottom), color);
        }

        DrawPanelFooter(m_Layout->menu_left, m_Layout->menu_right, box_bottom, submenu ? L"A: select    B: back    Start: close" : L"A: select    B: close");
        m_Ui.EndFrame();

        // Polled every frame; nothing until the button that opened it is released
        if (!ReadInput())
        {
            repeat.Step(0);
            continue;
        }

        const int direction = (m_DigitalButtons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (m_DigitalButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
        const int move = repeat.Step(direction);
        if (move != 0)
        {
            // At least one item is enabled (checked above), so this ends
            const int step = (move < 0) ? count - 1 : 1;
            do
            {
                selected = (selected + step) % count;
            } while (!enabled[selected]);
            continue;
        }

        if (m_DigitalButtons & XINPUT_GAMEPAD_START)
        {
            m_WaitForRelease = true;
            return MENU_RESULT_CLOSE;
        }

        if (m_AnalogButton == XINPUT_GAMEPAD_B)
        {
            m_WaitForRelease = true;
            return MENU_RESULT_BACK;
        }

        if (m_AnalogButton == XINPUT_GAMEPAD_A && enabled[selected])
        {
            m_WaitForRelease = true;
            return selected;
        }
    }
}

// Order of the Start menu, must match labels[] in DisplayStartMenu()
enum MenuItem
{
    MENU_EDIT_PARTITION = 0,
    MENU_FORMAT_SELECTED,
    MENU_UNDO_ALL,
    MENU_PARTITION_TABLE,
    MENU_SYSTEM_INFO,
    MENU_THEME,
    MENU_EXIT,
    MENU_ITEM_COUNT
};

void App::DisplayStartMenu()
{
    // Reopened when the user backs out of a submenu, on the item they came from
    int selected = 0;

    while (true)
    {
        wchar_t edit_label[60];
        swprintf(edit_label, L"Edit partition %d (%c)", m_CurrentPart + 1, partition_letter(m_CurrentPart));

        wchar_t format_label[60];
        swprintf(format_label, L"Format partition %d (%d KB clusters)", m_CurrentPart + 1, m_pTable->CalculateClusterSizeForPartition(m_CurrentPart));

        // Indexed by MenuItem
        const LPCWSTR labels[MENU_ITEM_COUNT] = {
            edit_label,
            format_label,
            L"Undo all changes since the last write",
            L"Partition table...",
            L"System information",
            L"Theme...",
            L"Exit to dashboard",
        };

        bool enabled[MENU_ITEM_COUNT];
        for (int i = 0; i < MENU_ITEM_COUNT; i++)
        {
            enabled[i] = true;
        }
        enabled[MENU_FORMAT_SELECTED] = m_pTable->isPartitionEnabled(m_CurrentPart);
        enabled[MENU_UNDO_ALL] = CheckIfPartitionTableChanged();

        selected = RunMenu(L"Menu", labels, enabled, MENU_ITEM_COUNT, selected, false);

        switch (selected)
        {
        case MENU_EDIT_PARTITION:
            DisplayEditPartition();
            return;
        case MENU_FORMAT_SELECTED:
            DoFormatOne();
            return;
        case MENU_PARTITION_TABLE:
            if (DisplayPartitionTableMenu())
            {
                return;
            }
            break; // Back: show this menu again
        case MENU_UNDO_ALL:
            UndoAllChanges();
            return;
        case MENU_SYSTEM_INFO:
            DisplaySystemInfo();
            return;
        case MENU_THEME:
            if (DisplayThemeMenu())
            {
                return;
            }
            break; // Back: show this menu again
        case MENU_EXIT:
            if (ConfirmExit())
            {
                QuitToDashboard();
            }
            return;
        default:
            // B or Start on the top level menu: closed
            return;
        }
    }
}

// The theme list: moving through it shows each theme right away, A keeps the
// selected one (and saves it for next time), B goes back to the theme that was
// in use. Returns false if the user went back with B.
bool App::DisplayThemeMenu()
{
    static const int MAX_THEMES = 32;
    LPCWSTR labels[MAX_THEMES];
    bool enabled[MAX_THEMES];
    int count = Renderer::GetThemeCount();
    if (count > MAX_THEMES)
    {
        count = MAX_THEMES;
    }
    for (int i = 0; i < count; i++)
    {
        labels[i] = Renderer::GetThemeAt(i).name;
        enabled[i] = true;
    }

    const int original = m_Ui.GetThemeIndex();
    const int chosen = RunMenu(L"Theme", labels, enabled, count, original, true, &App::PreviewTheme);

    if (chosen < 0)
    {
        // Back or closed: as it was
        m_Ui.SetTheme(original);
        return chosen == MENU_RESULT_CLOSE;
    }

    m_Ui.SetTheme(chosen);
    if (chosen != original)
    {
        SaveThemeSetting();
    }
    return true;
}

// The theme is kept in a small text file next to the xbe ("theme=Name"), by
// name so the list can change. A missing or unknown file means the default.
static const char THEME_SETTING_PATH[] = "D:\\xbpartitioner.ini";
static const char THEME_SETTING_KEY[] = "theme=";

void App::LoadThemeSetting()
{
    HANDLE file = CreateFile(THEME_SETTING_PATH, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    char text[128];
    DWORD read = 0;
    const BOOL ok = ReadFile(file, text, sizeof(text) - 1, &read, NULL);
    CloseHandle(file);
    if (!ok)
    {
        return;
    }
    text[read] = '\0';

    // "theme=Name", up to the end of the line
    char *value = strstr(text, THEME_SETTING_KEY);
    if (value == NULL)
    {
        return;
    }
    value += sizeof(THEME_SETTING_KEY) - 1;
    for (char *end = value; *end != '\0'; end++)
    {
        if (*end == '\r' || *end == '\n')
        {
            *end = '\0';
            break;
        }
    }

    // Spaces and tabs around the name (hand edited files)
    while (*value == ' ' || *value == '\t')
    {
        value++;
    }
    for (int length = (int)strlen(value); length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t'); length--)
    {
        value[length - 1] = '\0';
    }

    const int index = Renderer::FindTheme(value);
    if (index >= 0)
    {
        m_Ui.SetTheme(index);
    }
}

// Best effort and silent: it can't be written when running from a DVD, and
// isn't once D:'s partition changed (m_ThemeSaveBlocked). The theme is then
// only kept until exit.
void App::SaveThemeSetting()
{
    if (m_ThemeSaveBlocked)
    {
        return;
    }

    // Theme names are plain ASCII
    char text[80];
    const wchar_t *name = m_Ui.GetTheme().name;
    int length = sprintf(text, "%s", THEME_SETTING_KEY);
    for (int i = 0; name[i] != L'\0' && length < (int)sizeof(text) - 3; i++)
    {
        text[length++] = (char)name[i];
    }
    text[length++] = '\r';
    text[length++] = '\n';

    HANDLE file = CreateFile(THEME_SETTING_PATH, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD written = 0;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

void App::FormatLaunchPartitionLine(wchar_t *out, LPCWSTR what)
{
    swprintf(out, L"Partition %d (%c) holds this program: %s", m_LaunchPartition + 1,
             partition_letter(m_LaunchPartition), what);
}

bool App::MovesLaunchPartition(const XboxPartitionTable &table) const
{
    if (m_LaunchPartition < 0 || !m_pTableOriginal->isPartitionEnabled(m_LaunchPartition))
    {
        return false;
    }

    const XboxPartitionTableManipulator written(table, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    return !written.isPartitionEnabled(m_LaunchPartition) ||
           written.GetPartitionStart(m_LaunchPartition) != m_pTableOriginal->GetPartitionStart(m_LaunchPartition) ||
           written.GetPartitionSize(m_LaunchPartition) != m_pTableOriginal->GetPartitionSize(m_LaunchPartition);
}

// Order of the Partition table menu, must match labels[] in DisplayPartitionTableMenu()
enum TableMenuItem
{
    TABLE_MENU_WRITE = 0,
    TABLE_MENU_WRITE_AND_FORMAT,
    TABLE_MENU_NEW_TABLE,
    TABLE_MENU_LOAD_TABLE,
    TABLE_MENU_ERASE,
    TABLE_MENU_ITEM_COUNT
};

// Everything that works on the table as a whole: writing it, starting a new one
// from a preset, loading the kernel/HDD/backup table into the editor (both in
// submenus), and erasing the table on disk. Returns false if the user went back with B.
bool App::DisplayPartitionTableMenu()
{
    // Indexed by TableMenuItem
    static const LPCWSTR labels[TABLE_MENU_ITEM_COUNT] = {
        L"Write partition table",
        L"Write table and format changed partitions",
        L"New partition table...",
        L"Load partition table...",
        L"Erase partition table (stock layout)",
    };

    // Reopened on the same item when the user backs out of a submenu
    int selected = 0;

    while (true)
    {
        bool enabled[TABLE_MENU_ITEM_COUNT];
        for (int i = 0; i < TABLE_MENU_ITEM_COUNT; i++)
        {
            enabled[i] = true;
        }
        // Nothing to pick from if none of the tables can be loaded
        bool loadable[LOADED_TABLE_COUNT];
        enabled[TABLE_MENU_LOAD_TABLE] = GetLoadableTables(loadable);

        selected = RunMenu(L"Partition table", labels, enabled, TABLE_MENU_ITEM_COUNT, selected, true);

        switch (selected)
        {
        case MENU_RESULT_BACK:
            return false;
        case TABLE_MENU_WRITE:
            DoWriteTable();
            return true;
        case TABLE_MENU_WRITE_AND_FORMAT:
            DoFormatAll();
            return true;
        case TABLE_MENU_NEW_TABLE:
            if (DisplayNewTableMenu())
            {
                return true;
            }
            break; // Back: show this menu again
        case TABLE_MENU_LOAD_TABLE:
            if (DisplayLoadTableMenu())
            {
                return true;
            }
            break; // Back: show this menu again
        case TABLE_MENU_ERASE:
            DoEraseTable();
            return true;
        default:
            return true;
        }
    }
}

// Preset layouts, in the order of the New partition table menu (see ApplyPreset())
enum Preset
{
    PRESET_STOCK = 0,
    PRESET_F,
    PRESET_F_G,
    PRESET_COUNT
};

// Replaces the edited table with a preset layout (nothing is written).
// Returns false if the user went back with B.
bool App::DisplayNewTableMenu()
{
    // Indexed by Preset
    static const LPCWSTR labels[PRESET_COUNT] = {
        L"Stock partitions only",
        L"Stock + F (rest of the disk)",
        L"Stock + F and G (split the rest)",
    };

    static const bool enabled[PRESET_COUNT] = {true, true, true};

    const int choice = RunMenu(L"New partition table", labels, enabled, PRESET_COUNT, 0, true);
    if (choice == MENU_RESULT_BACK)
    {
        return false;
    }

    if (choice >= 0)
    {
        ApplyPreset(choice);
    }
    return true;
}

// Which tables can be loaded, indexed by LoadedTable. Tables that are missing or
// unreadable can't: loading those would wipe the layout. The kernel table needs
// an lba48 patch. Returns true if at least one can.
bool App::GetLoadableTables(bool loadable[LOADED_TABLE_COUNT])
{
    loadable[LOADED_TABLE_KERNEL] = (m_pKernelTable != NULL);
    loadable[LOADED_TABLE_HDD] = IsMainTableUsable();
    loadable[LOADED_TABLE_HDD_BACKUP] = IsBackupTableUsable();

    return loadable[LOADED_TABLE_KERNEL] || loadable[LOADED_TABLE_HDD] || loadable[LOADED_TABLE_HDD_BACKUP];
}

// Replaces the edited table with the kernel's, the HDD's or the HDD backup
// (nothing is written). Returns false if the user went back with B.
bool App::DisplayLoadTableMenu()
{
    // Indexed by LoadedTable
    static const LPCWSTR labels[LOADED_TABLE_COUNT] = {
        L"Kernel partition table",
        L"Partition table from HDD",
        L"Backup partition table from HDD",
    };

    bool loadable[LOADED_TABLE_COUNT];
    GetLoadableTables(loadable);

    const int choice = RunMenu(L"Load partition table", labels, loadable, LOADED_TABLE_COUNT, 0, true);
    if (choice == MENU_RESULT_BACK)
    {
        return false;
    }

    if (choice >= 0)
    {
        LoadTable((LoadedTable)choice);
    }
    return true;
}

// Replaces the edited table with one of the preset layouts. Nothing is written.
// If the current table already has the preset's layout nothing changes, so the
// "Current table" label keeps saying where the table came from.
void App::ApplyPreset(int preset)
{
    // Build the preset on a copy first to compare it with the current table
    XboxPartitionTableManipulator preview(m_pTable->GetPartitionTable(), m_TotalHddSectors, m_FullLba48, m_Physical4k);

    switch (preset)
    {
    case PRESET_STOCK:
        preview.SetupPartitionsStock();
        break;
    case PRESET_F:
        preview.SetupPartitionsFOnly();
        break;
    case PRESET_F_G:
        preview.SetupPartitionsFGSplit();
        break;
    default:
        return;
    }

    if (!preview.HasSameLayout(*m_pTable))
    {
        m_pTable->LoadNewTable(preview.GetPartitionTable());
        m_LoadedTable = LOADED_TABLE_PRESET;
    }
}

// Replaces the edited table with the kernel's, the HDD's or the HDD backup.
// Nothing is written: write it from the menu to make it the active table.
// No confirmation, like the presets: this only changes the editor.
void App::LoadTable(LoadedTable which)
{
    switch (which)
    {
    case LOADED_TABLE_KERNEL:
        m_pTable->LoadNewTable(m_pTableOriginal->GetPartitionTable());
        break;
    case LOADED_TABLE_HDD:
        m_pTable->LoadNewTable(m_TableHdd);
        break;
    case LOADED_TABLE_HDD_BACKUP:
        m_pTable->LoadNewTable(m_TableHddBackup);
        break;
    default:
        return;
    }

    m_LoadedTable = which;
}

// Fields on the edit partition page, top to bottom
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

// Step sizes for start/size on the edit page, in sectors
static const ULONG g_EditSteps[] = {0x8, 0x800, 0x40000, 0x200000, 0x1400000, 0xC800000};
static const LPCWSTR g_EditStepNames[] = {L"4 KB", L"1 MB", L"128 MB", L"1 GB", L"10 GB", L"100 GB"};
static const int EDIT_STEP_COUNT = ARRAY_COUNT(g_EditSteps);
static const int EDIT_DEFAULT_STEP = 3; // 1 GB

// The editor is an expanded menu, like the system information page: the
// fields are menu items, then a divider, then read-only details, all spaced
// like menu items in the wide info panel
static const int EDIT_DETAIL_COUNT = 5; // Name, format, on disk, free space, status

int App::EditPanelTop() const
{
    // Fields + half an item for the divider + details, centered on screen
    const int height = m_Layout->menu_first_item_offset + (EDIT_FIELD_COUNT + EDIT_DETAIL_COUNT) * MenuItemHeight() +
                       MenuItemHeight() / 2 + m_Layout->menu_footer_height;
    return (m_Layout->height - height) / 2;
}

// Page to edit the partition selected on the main screen.
// Up/Down: field, Left/Right: change it (held: repeats), A: toggle/apply, or
// on start, size and end: type a value (RunNumberInput),
// Y: reset this partition to the last written table, B or Start: back.
// Changes go into the edited table; nothing is written here.
// Runs one frame per pass so the selection bar slides between fields.
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
    DpadRepeat field_repeat;  // Up/down: choose a field
    DpadRepeat change_repeat; // Left/right: change start and size

    m_WaitForRelease = true;

    while (true)
    {
        const bool enabled = m_pTable->isPartitionEnabled(part);

        // Start, size and fill only make sense for an enabled partition
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

        // Nothing while a press that was acted on is still held
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
                // Next available field. Enabled is always available, so this ends.
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
            // Back to how this partition is in the last written table
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
            // The end stays put: forward shrinks the partition, back grows it
            // into the free space before. Held: repeats (change is 0 between repeats)
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
                // Grow as far as the free space after it goes (the manipulator clamps)
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

// Units the number input takes values in, in sectors (GB like the rest of the UI).
// Sectors can be typed in decimal or hex.
enum InputUnit
{
    INPUT_UNIT_SECTORS = 0,
    INPUT_UNIT_SECTORS_HEX,
    INPUT_UNIT_MB,
    INPUT_UNIT_GB,
    INPUT_UNIT_COUNT
};
static const ULONGLONG g_InputUnitSectors[INPUT_UNIT_COUNT] = {1, 1, 0x800, 0x200000};
static const LPCWSTR g_InputUnitNames[INPUT_UNIT_COUNT] = {L"sectors", L"sectors (hex)", L"MB", L"GB"};
static const LPCWSTR g_InputUnitShortNames[INPUT_UNIT_COUNT] = {L"sectors", L"sectors", L"MB", L"GB"};

static int input_base(int unit)
{
    return (unit == INPUT_UNIT_SECTORS_HEX) ? 16 : 10;
}

// A whole number of units as it is typed in that unit: 1234 or 0x4D2
static void format_units(ULONGLONG value, int unit, wchar_t *out)
{
    swprintf(out, (input_base(unit) == 16) ? L"0x%I64X" : L"%I64u", value);
}

// Number input keys: 0-15 are the digits (A-F only in hex)
enum KeypadKey
{
    KEY_DELETE = 16,
    KEY_UNIT,
    KEY_CLEAR,
    KEY_CANCEL,
    KEY_OK
};
static const int KEYPAD_COLUMNS = 3;
static const int KEYPAD_ROWS = 7;
static const int KEYPAD_HEX_ROWS = 2; // The top rows, A-F: skipped unless typing hex
static const int g_KeypadKeys[KEYPAD_ROWS][KEYPAD_COLUMNS] = {
    {0xA, 0xB, 0xC},
    {0xD, 0xE, 0xF},
    {7, 8, 9},
    {4, 5, 6},
    {1, 2, 3},
    {KEY_UNIT, 0, KEY_DELETE},
    {KEY_CANCEL, KEY_CLEAR, KEY_OK},
};
// Times a unit still fits in 64 bits, also in hex (60 bits)
static const int KEYPAD_MAX_DIGITS = 15;

// The typed digits as a number
static ULONGLONG parse_digits(const wchar_t *digits, int length, int base)
{
    ULONGLONG value = 0;
    for (int i = 0; i < length; i++)
    {
        const wchar_t d = digits[i];
        value = value * base + ((d >= L'A') ? (d - L'A' + 10) : (d - L'0'));
    }
    return value;
}

// Smallest and largest whole number of units in [min, max] sectors.
// false if there is none (the range is smaller than one unit).
static bool unit_range(ULONGLONG min, ULONGLONG max, int unit, ULONGLONG *min_units, ULONGLONG *max_units)
{
    const ULONGLONG per_unit = g_InputUnitSectors[unit];
    *min_units = min / per_unit + ((min % per_unit) ? 1 : 0);
    *max_units = max / per_unit;
    return *min_units <= *max_units;
}

// A on start, size or end in the editor: asks for the value and applies it.
// Typed values go through the same functions as Left/Right, so they are 4K
// aligned the same way; the footer says when that changed the value.
void App::TypeEditValue(int part, int field, LPCWSTR &message, Color &message_color)
{
    const ULONGLONG start = m_pTable->GetPartitionStart(part);
    const ULONGLONG size = m_pTable->GetPartitionSize(part);
    const ULONGLONG end = start + size;
    const ULONGLONG free_before = m_pTable->GetAvailableFreeSpaceForPartitionBefore(part);
    const ULONGLONG free_after = m_pTable->GetAvailableFreeSpaceForPartition(part);

    LPCWSTR title;
    ULONGLONG current, min, max;
    int unit;

    switch (field)
    {
    case EDIT_START:
        // The end stays put, so up to one 4 KB block before it. A zero size
        // partition has nothing to shrink and moves instead.
        title = L"Start sector";
        current = start;
        min = start - free_before;
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
        unit = INPUT_UNIT_SECTORS_HEX; // Shown in hex on the editor and main table
        break;

    case EDIT_SIZE:
        title = L"Size";
        current = size;
        min = 0;
        max = size + free_after;
        unit = INPUT_UNIT_GB;
        break;

    default: // EDIT_END
        title = L"End sector";
        current = end;
        min = start;
        max = end + free_after;
        unit = INPUT_UNIT_SECTORS_HEX; // Shown in hex on the editor and main table
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
        // The end stays, unless it wasn't 4K aligned (or the partition was
        // empty and moved)
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

    // In range (RunNumberInput checked), so only the alignment can change it
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

// On screen number pad over the current screen: D-pad picks a key (held:
// repeats), A presses it, X deletes a digit, Y changes the unit, Start is OK,
// B or Back cancels. The value is typed in whole units (sectors in decimal or
// hex, MB, GB; A-F are greyed out unless typing hex) and
// must be within min..max sectors. Returns false if cancelled; *result is in
// sectors.
bool App::RunNumberInput(LPCWSTR title, ULONGLONG current, ULONGLONG min, ULONGLONG max, int unit, ULONGLONG *result)
{
    wchar_t digits[KEYPAD_MAX_DIGITS + 1];
    int length = 0;
    digits[0] = L'\0';
    int row = KEYPAD_HEX_ROWS + 1, column = 1; // On the 5
    LPCWSTR message = NULL;
    wchar_t message_text[100];
    DpadRepeat vertical_repeat;
    DpadRepeat horizontal_repeat;
    // The A that opened it is still held
    m_WaitForRelease = true;

    while (true)
    {
        // Every frame (Present waits for the next one). Behind it: the main
        // screen and, from the editor, the editor.
        m_Ui.BeginFrame();
        DrawMainScreen();
        if (m_pActiveEditView != NULL)
        {
            DrawEditPanel(*m_pActiveEditView);
        }
        DrawNumberInput(title, digits, unit, current, min, max, g_KeypadKeys[row][column], message);
        m_Ui.EndFrame();

        // Nothing until a pressed key is released, so each press counts once
        if (!ReadInput())
        {
            vertical_repeat.Step(0);
            horizontal_repeat.Step(0);
            continue;
        }
        const int buttons = m_DigitalButtons;
        const int analog = m_AnalogButton;

        // D-pad: move between keys, stopping at the edges
        const int vertical = vertical_repeat.Step((buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0);
        const int horizontal = horizontal_repeat.Step((buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0);
        if (vertical != 0 || horizontal != 0)
        {
            const int new_row = row + vertical;
            const int new_column = column + horizontal;
            const int first_row = (input_base(unit) == 16) ? 0 : KEYPAD_HEX_ROWS;
            if (new_row >= first_row && new_row < KEYPAD_ROWS)
            {
                row = new_row;
            }
            if (new_column >= 0 && new_column < KEYPAD_COLUMNS)
            {
                column = new_column;
            }
        }

        int key = -1;
        if (analog == XINPUT_GAMEPAD_A)
        {
            key = g_KeypadKeys[row][column];
        }
        else if (analog == XINPUT_GAMEPAD_X)
        {
            key = KEY_DELETE;
        }
        else if (analog == XINPUT_GAMEPAD_Y)
        {
            key = KEY_UNIT;
        }
        else if (analog == XINPUT_GAMEPAD_B || (buttons & XINPUT_GAMEPAD_BACK))
        {
            key = KEY_CANCEL;
        }
        else if (buttons & XINPUT_GAMEPAD_START)
        {
            key = KEY_OK;
        }

        if (key < 0)
        {
            continue;
        }

        m_WaitForRelease = true;
        message = NULL;

        if (key <= 0xF)
        {
            if (key >= input_base(unit))
            {
                continue; // A-F outside hex (not reachable with the D-pad)
            }
            if (length == 1 && digits[0] == L'0')
            {
                length = 0; // No leading zeros
            }
            if (length < KEYPAD_MAX_DIGITS)
            {
                digits[length++] = (wchar_t)((key < 10) ? (L'0' + key) : (L'A' + key - 10));
                digits[length] = L'\0';
            }
            else
            {
                message = L"No more digits fit.";
            }
            continue;
        }

        switch (key)
        {
        case KEY_DELETE:
            if (length > 0)
            {
                digits[--length] = L'\0';
            }
            break;

        case KEY_CLEAR:
            length = 0;
            digits[0] = L'\0';
            break;

        case KEY_UNIT:
        {
            // Between decimal and hex the number stays the same, written in
            // the other base (sectors to hex and back, hex to MB)
            const int old_base = input_base(unit);
            unit = (unit + 1) % INPUT_UNIT_COUNT;
            const int new_base = input_base(unit);
            if (new_base != old_base && length > 0)
            {
                wchar_t converted[24];
                swprintf(converted, (new_base == 16) ? L"%I64X" : L"%I64u", parse_digits(digits, length, old_base));
                if ((int)wcslen(converted) <= KEYPAD_MAX_DIGITS)
                {
                    wcscpy(digits, converted);
                    length = (int)wcslen(digits);
                }
                else
                {
                    length = 0;
                    digits[0] = L'\0';
                }
            }
            if (new_base != 16 && row < KEYPAD_HEX_ROWS)
            {
                row = KEYPAD_HEX_ROWS; // Off the A-F keys
            }
            break;
        }

        case KEY_CANCEL:
            m_WaitForRelease = true;
            return false;

        case KEY_OK:
        {
            if (length == 0)
            {
                message = L"Type a number first.";
                break;
            }

            const ULONGLONG value = parse_digits(digits, length, input_base(unit));

            ULONGLONG min_units, max_units;
            if (!unit_range(min, max, unit, &min_units, &max_units))
            {
                message = L"Not a whole number of these, use a smaller unit.";
                break;
            }
            if (value < min_units || value > max_units)
            {
                wchar_t low[24], high[24];
                format_units(min_units, unit, low);
                format_units(max_units, unit, high);
                swprintf(message_text, L"Must be from %s to %s %s.", low, high, g_InputUnitShortNames[unit]);
                message = message_text;
                break;
            }

            // value <= max / unit, so this can't overflow
            *result = value * g_InputUnitSectors[unit];
            m_WaitForRelease = true;
            return true;
        }
        }
    }
}

void App::DrawNumberInput(LPCWSTR title, const wchar_t *digits, int unit, ULONGLONG current, ULONGLONG min, ULONGLONG max,
                          int selected_key, LPCWSTR message)
{
    // Dialog wide, centered, sized from the dialog and menu spacing
    const int left = m_Layout->dialog_left;
    const int right = m_Layout->dialog_right;
    const int inset = m_Layout->dialog_text_inset;
    const int gap = m_Layout->menu_selection_inset;
    const int key_height = m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->dialog_button_padding);
    const int key_width = (right - left - 2 * inset - (KEYPAD_COLUMNS - 1) * gap) / KEYPAD_COLUMNS;
    const int line_height = m_Layout->dialog_line_height;

    const int height = m_Layout->menu_first_item_offset + key_height + gap + 2 * line_height + gap +
                       KEYPAD_ROWS * (key_height + gap) - gap + m_Layout->menu_footer_height;
    const int top = (m_Layout->height - height) / 2;
    const int bottom = top + height;
    const int value_y = top + m_Layout->menu_first_item_offset;
    const int now_y = value_y + key_height + gap;
    const int range_y = now_y + line_height;
    const int keys_y = range_y + line_height + gap;

    wchar_t text[100];
    wchar_t amount[40];

    DrawPanel(left, top, right, bottom, title, value_y);

    // The value being typed, with a cursor, and its unit on the right
    m_Ui.Rect((FLOAT)(left + inset), (FLOAT)value_y, (FLOAT)(right - inset), (FLOAT)(value_y + key_height),
              Colors().selection, Colors().selection);
    swprintf(text, (input_base(unit) == 16) ? L"0x%s_" : L"%s_", digits);
    m_Ui.Text(FONT_TABLE, text, left + inset + gap, m_Ui.TextYCentered(FONT_TABLE, value_y, value_y + key_height), COLOR_YELLOW);
    m_Ui.Text(FONT_TEXT, g_InputUnitNames[unit], right - inset - gap, m_Ui.TextYCentered(FONT_TEXT, value_y, value_y + key_height),
              COLOR_WHITE, XFONT_RIGHT);

    // What it is now, and what it can be in this unit
    if (g_InputUnitSectors[unit] == 1)
    {
        format_units(current, unit, amount);
        wcscat(amount, L" sectors");
    }
    else
    {
        swprintf(amount, L"%.2f %s", (double)current / g_InputUnitSectors[unit], g_InputUnitNames[unit]);
    }
    swprintf(text, L"Now: %s", amount);
    m_Ui.Text(FONT_TEXT, text, left + inset, now_y, COLOR_GREY);

    ULONGLONG min_units, max_units;
    if (unit_range(min, max, unit, &min_units, &max_units))
    {
        wchar_t low[24], high[24];
        format_units(min_units, unit, low);
        format_units(max_units, unit, high);
        swprintf(text, L"Can be: %s to %s %s", low, high, g_InputUnitShortNames[unit]);
    }
    else
    {
        swprintf(text, L"Can be: less than one %s, use a smaller unit", g_InputUnitShortNames[unit]);
    }
    m_Ui.Text(FONT_TEXT, text, left + inset, range_y, COLOR_GREY);

    // Keys, like dialog buttons
    for (int r = 0; r < KEYPAD_ROWS; r++)
    {
        for (int c = 0; c < KEYPAD_COLUMNS; c++)
        {
            const int key = g_KeypadKeys[r][c];
            const int x = left + inset + c * (key_width + gap);
            const int y = keys_y + r * (key_height + gap);
            const bool selected = (key == selected_key);
            const bool usable = (key > 0xF || key < input_base(unit)); // A-F only in hex
            const D3DCOLOR color = selected ? Colors().selection : Colors().button;

            LPCWSTR label = text;
            switch (key)
            {
            case KEY_DELETE: label = L"Delete"; break;
            case KEY_UNIT:   label = L"Unit"; break;
            case KEY_CLEAR:  label = L"Clear"; break;
            case KEY_CANCEL: label = L"Cancel"; break;
            case KEY_OK:     label = L"OK"; break;
            default:         swprintf(text, L"%X", key); break;
            }

            m_Ui.Rect((FLOAT)x, (FLOAT)y, (FLOAT)(x + key_width), (FLOAT)(y + key_height), color, color);
            m_Ui.Text(FONT_TEXT, label, x + key_width / 2, m_Ui.TextYCentered(FONT_TEXT, y, y + key_height),
                      selected ? COLOR_YELLOW : (usable ? COLOR_WHITE : COLOR_GREY), XFONT_CENTER);
        }
    }

    if (message != NULL)
    {
        DrawPanelFooter(left, right, bottom, message, COLOR_RED);
    }
    else
    {
        DrawPanelFooter(left, right, bottom, L"A: press   X: delete   Y: unit   Start: OK   B: cancel");
    }
}

// An enabled partition left at size 0 is disabled: an empty entry in the table
// is never useful, and it would block the spot for nothing
void App::LeaveEditPartition(int part)
{
    if (m_pTable->isPartitionEnabled(part) && m_pTable->GetPartitionSize(part) == 0)
    {
        m_pTable->DisablePartition(part);
    }

    m_WaitForRelease = true;
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

    const int left = m_Layout->info_left;
    const int right = m_Layout->info_right;
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

    // Selection bar at its sliding position, before the field text
    const int selection_top = (int)(bar_y + 0.5f);
    m_Ui.Rect((FLOAT)(left + m_Layout->menu_selection_inset), (FLOAT)selection_top,
              (FLOAT)(right - m_Layout->menu_selection_inset), (FLOAT)(selection_top + bar_height),
              Colors().selection, Colors().selection);

    // Editable fields, like menu items
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
        // Start and end in hex, 12 digits like the main table
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
            format_human_size(m_pTable->GetAvailableFreeSpaceForPartition(part), human);
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

        // Both fonts are the same height in each mode, so label and value share a baseline
        m_Ui.Text(FONT_TEXT, label, label_x, m_Ui.TextYCentered(FONT_TEXT, bar_top, bar_top + bar_height), color);
        m_Ui.Text(FONT_TABLE, text, value_x, m_Ui.TextYCentered(FONT_TABLE, bar_top, bar_top + bar_height), color);
    }

    // Divider between the fields and the details, like the line under the title
    m_Ui.Rect((FLOAT)(left + m_Layout->menu_selection_inset), (FLOAT)divider_y,
              (FLOAT)(right - m_Layout->menu_selection_inset), (FLOAT)(divider_y + 1),
              Colors().panel_line, Colors().panel_line);

    // Read-only details, like the system information rows
    struct DetailRow
    {
        LPCWSTR label;
        wchar_t value[80];
        Font font;
        Color color;
    };
    DetailRow details[EDIT_DETAIL_COUNT];

    // Name is 16 single byte characters, space padded
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

    // Compared with the last written table
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
        swprintf(details[4].value, L"changed, unreadable until formatted");
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

// Replaces the table with the stock layout on disk (main and backup)
void App::DoEraseTable()
{
    static const LPCWSTR lines[] = {
        L"The partition table on the hard drive will be replaced",
        L"by the stock layout. Extended partitions are lost.",
    };
    if (!ConfirmWarning(lines, ARRAY_COUNT(lines), L"Erase"))
    {
        return;
    }

    if (erase_partition_tables())
    {
        ShowMessage(L"Done", COLOR_GREEN, L"The partition table was replaced by the stock layout.");
    }
    else
    {
        ShowMessage(L"Error", COLOR_RED, L"The partition table could not be erased.");
    }
}

bool App::format_partition(const int i)
{
    // D: is on this partition, or it's unknown where D: is
    if (m_LaunchPartition < 0 || i == m_LaunchPartition)
    {
        m_ThemeSaveBlocked = true;
    }

    const int cluster_size = m_pTable->CalculateClusterSizeForPartition(i);
    const ULONG cluster_size_bytes = cluster_size << 10;

    // XapiFormatFATVolumeEx returns a BOOL (TRUE on success), not an NTSTATUS
    const bool formatted = XapiFormatFATVolumeEx(&m_PartitionPaths[i], cluster_size_bytes) != FALSE;
    const bool fixed_up = formatted && largePartitionFixup(&m_PartitionPaths[i], cluster_size_bytes);

    // The FATX header changed (or may be half written), re-read what is on disk
    m_ClusterSizes[i] = get_cluster_size_from_partition(m_PartitionPaths[i]);

    if (!formatted || !fixed_up)
    {
        wchar_t error[60];
        if (!formatted)
        {
            swprintf(error, L"Partition %d could not be formatted.", i + 1);
        }
        else
        {
            swprintf(error, L"Partition %d: setting the cluster size failed.", i + 1);
        }
        ShowMessage(L"Error", COLOR_RED, error);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Controller polling
//-----------------------------------------------------------------------------

// Reads the first controller with something pressed.
// *analog: index of the pressed analog button (A, B, X, Y, Black, White,
// triggers), -1 if none. *buttons: digital buttons (D-pad, Start, Back, thumbs).
// wait: 0 = poll once, 1 = wait for a press, 2 = wait for a press and its release.
// Returns the controller index, or -1 if nothing was pressed (wait = 0).
// Analog buttons count as pressed above XINPUT_GAMEPAD_MAX_CROSSTALK, as the XDK
// recommends: worn or third party pads can report small values at rest, which
// would otherwise look like a button that is never released.
int App::XInputGetStateAny(int *analog, int *buttons, int wait)
{
    XINPUT_STATE ControllerState[4];
    int c = -1;
    *analog = -1;
    *buttons = 0;

    do
    {
        for (int i = 0; i < 4 && c == -1; i++)
        {
            if (!m_Controllers[i])
            {
                continue;
            }

            XInputGetState(m_Controllers[i], &ControllerState[i]);

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
        XInputGetState(m_Controllers[c], &ControllerState[c]);

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

//-----------------------------------------------------------------------------
// Disk and kernel partition table access
//-----------------------------------------------------------------------------

ULONGLONG App::get_hdd_size()
{
    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\partition0");

    HANDLE handle;
    OBJECT_ATTRIBUTES obj_attr;
    obj_attr.RootDirectory = 0;
    obj_attr.ObjectName = &a_file;
    obj_attr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io_stat_block;
    unsigned int stat = NtOpenFile(&handle, (GENERIC_READ | 0x00100000),
                                   &obj_attr, &io_stat_block, (FILE_SHARE_READ | FILE_SHARE_WRITE), 0x10);

    if (stat != STATUS_SUCCESS)
    {
        // 0 means unknown size; returning the NTSTATUS would be read as a sector count
        return 0;
    }

    unsigned int geom_ioctl_cmd_in_buf[100];
    memset(geom_ioctl_cmd_in_buf, 0, sizeof(geom_ioctl_cmd_in_buf));
    geom_ioctl_cmd_in_buf[0] = IOCTL_SUBCMD_GET_INFO;

    DISK_GEOMETRY DiskGeometry;
    stat = NtDeviceIoControlFile(handle, 0, 0, 0, &io_stat_block,
                                 IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                 geom_ioctl_cmd_in_buf, sizeof(geom_ioctl_cmd_in_buf),
                                 &DiskGeometry, sizeof(DiskGeometry));
    NtClose(handle);

    ULONGLONG total_sectors = 0ULL;
    if (stat == STATUS_SUCCESS)
    {
        total_sectors = DiskGeometry.Cylinders.QuadPart;
    }

    return total_sectors;
}

// IOCTL_IDE_PASS_THROUGH input/output, as used by the Xbox IDE driver
typedef struct _XB_IDE_REGS
{
    UCHAR bFeaturesReg;
    UCHAR bSectorCountReg;
    UCHAR bSectorNumberReg;
    UCHAR bCylLowReg;
    UCHAR bCylHighReg;
    UCHAR bDriveHeadReg;
    UCHAR bCommandReg;
    UCHAR bHostSendsData;
} XB_IDE_REGS;

typedef struct _XB_ATA_PASS_THROUGH
{
    XB_IDE_REGS IdeReg;
    ULONG DataBufferSize;
    PVOID DataBuffer;
} XB_ATA_PASS_THROUGH;

#define ATA_COMMAND_IDENTIFY_DEVICE 0xEC
#define ATA_DRIVE_HEAD_MASTER 0xA0

// ATA IDENTIFY DEVICE words used here
#define ATA_ID_FIRMWARE_WORD 23           // 4 words, 8 characters
#define ATA_ID_MODEL_WORD 27              // 20 words, 40 characters
#define ATA_ID_LBA28_SECTORS_WORD 60      // 2 words
#define ATA_ID_COMMAND_SET_2_WORD 83      // bit 10: 48 bit addressing supported
#define ATA_ID_LBA48_SECTORS_WORD 100     // 4 words
#define ATA_ID_SECTOR_SIZE_WORD 106       // physical/logical sector size

// ATA strings store two characters per word, high byte first, padded with spaces
static void ata_string(const USHORT *words, int word_count, wchar_t *out)
{
    int length = 0;
    for (int i = 0; i < word_count; i++)
    {
        out[length++] = (wchar_t)(words[i] >> 8);
        out[length++] = (wchar_t)(words[i] & 0xFF);
    }

    // Trim trailing spaces (and NULs some drives pad with)
    while (length > 0 && (out[length - 1] == L' ' || out[length - 1] == L'\0'))
    {
        length--;
    }
    out[length] = L'\0';

    // And leading spaces
    int skip = 0;
    while (out[skip] == L' ')
    {
        skip++;
    }
    if (skip > 0)
    {
        memmove(out, out + skip, (length - skip + 1) * sizeof(wchar_t));
    }
}

// Asks the drive itself (ATA IDENTIFY DEVICE through IOCTL_IDE_PASS_THROUGH)
// for its model, firmware, native size and sector size. On failure
// m_Drive.identify_ok is false and the drive is treated as a plain 512 byte one.
void App::read_drive_identify()
{
    memset(&m_Drive, 0, sizeof(m_Drive));

    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\Partition0");

    OBJECT_ATTRIBUTES obj_attr;
    InitializeObjectAttributes(&obj_attr, &a_file, OBJ_CASE_INSENSITIVE, NULL);

    HANDLE handle;
    IO_STATUS_BLOCK io_stat_block;
    NTSTATUS stat = NtOpenFile(&handle, SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE, &obj_attr, &io_stat_block,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(stat))
    {
        return;
    }

    USHORT identify[256];
    memset(identify, 0, sizeof(identify));

    XB_ATA_PASS_THROUGH ata;
    memset(&ata, 0, sizeof(ata));
    ata.IdeReg.bSectorCountReg = 1;
    ata.IdeReg.bDriveHeadReg = ATA_DRIVE_HEAD_MASTER;
    ata.IdeReg.bCommandReg = ATA_COMMAND_IDENTIFY_DEVICE;
    ata.IdeReg.bHostSendsData = FALSE;
    ata.DataBufferSize = sizeof(identify);
    ata.DataBuffer = identify;

    stat = NtDeviceIoControlFile(handle, NULL, NULL, NULL, &io_stat_block, IOCTL_IDE_PASS_THROUGH,
                                 &ata, sizeof(ata), &ata, sizeof(ata));
    NtClose(handle);

    if (!NT_SUCCESS(stat))
    {
        return;
    }

    m_Drive.identify_ok = true;
    ata_string(&identify[ATA_ID_MODEL_WORD], 20, m_Drive.model);
    ata_string(&identify[ATA_ID_FIRMWARE_WORD], 4, m_Drive.firmware);

    // Native size: 48 bit count if the drive supports it, else the 28 bit one
    if (identify[ATA_ID_COMMAND_SET_2_WORD] & 0x0400)
    {
        m_Drive.native_sectors = (ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD] |
                                 ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 1] << 16) |
                                 ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 2] << 32) |
                                 ((ULONGLONG)identify[ATA_ID_LBA48_SECTORS_WORD + 3] << 48);
    }
    else
    {
        m_Drive.native_sectors = (ULONGLONG)identify[ATA_ID_LBA28_SECTORS_WORD] |
                                 ((ULONGLONG)identify[ATA_ID_LBA28_SECTORS_WORD + 1] << 16);
    }

    // Word 106:
    //   bits 15:14 = 01  word is valid (else: old drive, 512 byte physical sectors)
    //   bit 13         multiple logical sectors per physical sector
    //   bits 3:0       log2(logical sectors per physical sector), 3 = 8 = 4K
    m_Drive.physical_sector_size = HDD_SECTOR_SIZE;
    const USHORT sector_size_info = identify[ATA_ID_SECTOR_SIZE_WORD];
    if ((sector_size_info & 0xC000) == 0x4000 && (sector_size_info & 0x2000) != 0)
    {
        m_Drive.physical_sector_size = HDD_SECTOR_SIZE << (sector_size_info & 0x000F);
    }

    // 512e: 4K (or larger) physical sectors behind 512 byte logical ones
    m_Drive.physical_4k = m_Drive.physical_sector_size >= 4096;
}

bool App::read_lba48_info(XboxPartitionTable *p_table)
{
    ANSI_STRING a_file;
    RtlInitAnsiString(&a_file, "\\Device\\Harddisk0\\partition0");

    HANDLE handle;
    OBJECT_ATTRIBUTES obj_attr;
    obj_attr.RootDirectory = 0;
    obj_attr.ObjectName = &a_file;
    obj_attr.Attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io_stat_block;
    unsigned int stat = NtOpenFile(&handle, (GENERIC_READ | 0x00100000),
                                   &obj_attr, &io_stat_block, (FILE_SHARE_READ | FILE_SHARE_WRITE), 0x10);

    if (stat != STATUS_SUCCESS)
    {
        return false;
    }

    memset(p_table, 0, sizeof(XboxPartitionTable));

    unsigned int lba48_ioctl_cmd_in_buf[100];
    unsigned int lba48_ioctl_cmd_out_buf[100];

    memset(lba48_ioctl_cmd_in_buf, 0, sizeof(lba48_ioctl_cmd_in_buf));
    lba48_ioctl_cmd_in_buf[0] = IOCTL_SUBCMD_GET_INFO;

    memset(lba48_ioctl_cmd_out_buf, 0, sizeof(lba48_ioctl_cmd_out_buf));

    stat = NtDeviceIoControlFile(handle, 0, 0, 0, &io_stat_block,
                                 IOCTL_CMD_LBA48_ACCESS,
                                 lba48_ioctl_cmd_in_buf, sizeof(lba48_ioctl_cmd_in_buf),
                                 lba48_ioctl_cmd_out_buf, sizeof(lba48_ioctl_cmd_out_buf));

    NtClose(handle);
    if (stat != STATUS_SUCCESS)
    {
        return false;
    }

    if ((lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_MAGIC1_IDX] != LBA48_GET_INFO_MAGIC1_VAL) ||
        (lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_MAGIC2_IDX] != LBA48_GET_INFO_MAGIC2_VAL))
    {
        return false;
    }

    // this way is compatible with Paul's lba48 patched kernels
    unsigned int partition_table_addr = lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_LOWCODE_BASE_IDX];
    partition_table_addr += lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_PART_TABLE_OFS_IDX];

    m_Lba48Version = lba48_ioctl_cmd_out_buf[LBA48_GET_INFO_PATCHCODE_VERSION_IDX];
    m_FullLba48 = (m_Lba48Version > 3);

    if (m_Lba48Version == 2)
    {
        partition_table_addr += 0x200;
    }

    m_pKernelTable = (XboxPartitionTable *)partition_table_addr;

    // Read partition table from ram?
    memcpy(p_table, (void *)partition_table_addr, sizeof(XboxPartitionTable));

    return true;
}

bool App::reload_lba48()
{
    if (m_pTable != NULL)
    {
        delete (m_pTable);
        m_pTable = NULL;
    }

    if (m_pTableOriginal != NULL)
    {
        delete (m_pTableOriginal);
        m_pTableOriginal = NULL;
    }

    // Read kernel patch info + kernel reported partition table
    if (read_lba48_info(&m_TableKernel))
    {
        m_pTable = new XboxPartitionTableManipulator(m_TableKernel, m_TotalHddSectors, m_FullLba48, m_Physical4k);
        m_pTableOriginal = new XboxPartitionTableManipulator(m_TableKernel, m_TotalHddSectors, m_FullLba48, m_Physical4k);
        return true;
    }
    else
    {
        m_pTable = new XboxPartitionTableManipulator(m_TotalHddSectors, m_FullLba48, m_Physical4k);
        m_pTableOriginal = new XboxPartitionTableManipulator(m_TotalHddSectors, m_FullLba48, m_Physical4k);
        return false;
    }
}

unsigned int App::get_cluster_size_from_partition(ANSI_STRING part)
{
    HANDLE hDisk;
    NTSTATUS status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    BYTE PartSector[HDD_SECTOR_SIZE * 8];
    LARGE_INTEGER RWOffset;
    FATX_SUPERBLOCK fsb;

    InitializeObjectAttributes(&ObjectAttributes, &part, OBJ_CASE_INSENSITIVE, NULL);

    RWOffset.QuadPart = 0;
    memset(PartSector, 0, sizeof(PartSector));

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_READ, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);

    if (!NT_SUCCESS(status))
    {
        return 0;
    }

    status = NtReadFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, sizeof(PartSector), &RWOffset);
    NtClose(hDisk);

    if (!NT_SUCCESS(status))
    {
        return 0;
    }

    memcpy(&fsb, PartSector, sizeof(fsb) < sizeof(PartSector) ? sizeof(fsb) : sizeof(PartSector));

    // Unformatted partitions, or ones holding something else, have no FATX header:
    // whatever is at the ClusterSize offset is not a cluster size
    if (memcmp(fsb.Tag, "FATX", 4) != 0)
    {
        return 0;
    }

    // ClusterSize is in sectors and always a power of two
    const uint32_t sectors_per_cluster = fsb.ClusterSize;
    if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0)
    {
        return 0;
    }

    // Sectors (512 bytes) to KB
    return sectors_per_cluster / 2;
}

void App::RefreshClusterSizes()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        m_ClusterSizes[i] = get_cluster_size_from_partition(m_PartitionPaths[i]);
    }
}

// True if the edited table differs from the last written/loaded one in any
// way the kernel cares about: a partition enabled/disabled, moved or resized
bool App::CheckIfPartitionTableChanged()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const bool enabled = m_pTable->isPartitionEnabled(i);
        if (enabled != m_pTableOriginal->isPartitionEnabled(i))
        {
            return true;
        }

        if (enabled && (m_pTable->GetPartitionStart(i) != m_pTableOriginal->GetPartitionStart(i) ||
                        m_pTable->GetPartitionSize(i) != m_pTableOriginal->GetPartitionSize(i)))
        {
            return true;
        }
    }
    return false;
}

// Marks partitions that are enabled now and whose start or size differs from
// the last written table. With existing_only, only partitions that were
// already enabled before (so may hold data) are marked.
// Returns how many were marked.
int App::GetChangedPartitions(bool changed[XboxPartitionTableEntryCount], bool existing_only)
{
    int count = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const bool was_enabled = m_pTableOriginal->isPartitionEnabled(i);
        changed[i] = m_pTable->isPartitionEnabled(i) &&
                     (!existing_only || was_enabled) &&
                     (!was_enabled ||
                      m_pTable->GetPartitionStart(i) != m_pTableOriginal->GetPartitionStart(i) ||
                      m_pTable->GetPartitionSize(i) != m_pTableOriginal->GetPartitionSize(i));
        if (changed[i])
        {
            count++;
        }
    }

    return count;
}

// "1 (E), 2 (C), 6 (F)" for the marked partitions
void App::FormatPartitionList(const bool marked[XboxPartitionTableEntryCount], wchar_t *out, size_t out_count)
{
    out[0] = L'\0';
    size_t used = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!marked[i])
        {
            continue;
        }

        wchar_t item[16];
        swprintf(item, L"%s%d (%c)", used ? L", " : L"", i + 1, partition_letter(i));

        const size_t item_length = wcslen(item);
        if (used + item_length + 1 > out_count)
        {
            break;
        }
        wcscat(out, item);
        used += item_length;
    }
}

LARGE_INTEGER App::get_backup_partition_offset_bytes()
{
    // The manipulator decides where the backup goes (last sector, or the last 4K
    // block on 512e drives, capped for pre v4 lba48) and keeps partitions clear of it
    LARGE_INTEGER byte_pos;
    byte_pos.QuadPart = (ULONGLONG)m_pTable->GetBackupTableLba() * (ULONGLONG)HDD_SECTOR_SIZE;
    return byte_pos;
}

// Writes the main table and, when possible, the backup; *backup_written says
// whether the backup was written too.
bool App::write_partition_tables(const XboxPartitionTable table, bool *backup_written)
{
    if (backup_written != NULL)
    {
        *backup_written = false;
    }

    // Without a known disk size nothing about the layout can be trusted
    if (m_TotalHddSectors == 0)
    {
        return false;
    }

    // The backup is skipped:
    //   - without an lba48 patch: the stock kernel reports at most ~128GB (28 bit
    //     LBAs), so on a bigger drive its "last sector" is in the middle of the disk
    //   - when there is no room for it (stock 8GB drive)
    //   - when the last partition of the table being written covers its sector:
    //     the backup would overwrite the end of that partition. Writing a table
    //     that leaves room for it (e.g. a new one from a preset) brings it back.
    const XboxPartitionTableManipulator to_write(table, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    const bool write_backup = (m_pKernelTable != NULL) && (m_pTable->GetBackupTableLba() != 0) && !to_write.OverlapsBackupTable();

    LARGE_INTEGER RWOffset;
    RWOffset.QuadPart = 0;
    LARGE_INTEGER RWOffset_backup = get_backup_partition_offset_bytes();

    BYTE PartSector[HDD_SECTOR_SIZE]; // XboxPartitionTable should be the same size as HDD_SECTOR_SIZE
    memcpy(PartSector, &table, HDD_SECTOR_SIZE);

    // Before anything is written: from here D: may no longer match its partition
    if (m_LaunchPartition < 0 || MovesLaunchPartition(table))
    {
        m_ThemeSaveBlocked = true;
    }

    HANDLE hDisk = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ANSI_STRING a_path;
    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);

    // Backup first: if it fails nothing has been touched yet. If the main write then
    // fails, the mismatch check on the next boot will point it out.
    if (status == 0 && write_backup)
    {
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset_backup);
    }

    if (status == 0)
    {
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset);
    }

    // This shouldn't really be here, but both the kernel and the internal XBpartitioner logic need to know about the new partition table
    if (status == 0 && m_pKernelTable)
    {
        // p_table_add
        memcpy(m_pKernelTable, &table, sizeof(XboxPartitionTable));
        memcpy(&m_TableKernel, &table, sizeof(XboxPartitionTable));
    }

    if (hDisk != NULL)
    {
        NtClose(hDisk);
    }

    if (status != 0)
    {
        return false;
    }

    if (backup_written != NULL)
    {
        *backup_written = write_backup;
    }

    if (m_pKernelTable)
    {
        // Rebuild both editor tables from the kernel's (now updated) table
        reload_lba48();
    }
    else
    {
        // No lba48 patch: reload_lba48() would fail and reset the editor to the
        // stock layout. The write itself succeeded, so the written table is the
        // new baseline. The kernel only picks it up after a reboot.
        m_pTable->LoadNewTable(table);
        m_pTableOriginal->LoadNewTable(table);
    }

    // Refresh the cached on-disk copies. After the editor tables: reading the
    // backup checks the written layout (m_pTableOriginal) for an overlap.
    m_TableHdd = read_hdd_main_partition_table();
    m_TableHddBackup = read_hdd_backup_partition_table();

    // The editor now holds what was written, which is also the kernel's table
    m_LoadedTable = LOADED_TABLE_KERNEL;

    // Partitions may now point at different sectors
    RefreshClusterSizes();
    return true;
}

// Writes the stock layout. Built on a copy, so if the write fails the editor
// still has the user's edits; on success write_partition_tables() makes the
// stock table the new baseline for the editor too.
bool App::erase_partition_tables()
{
    XboxPartitionTableManipulator stock(m_pTable->GetPartitionTable(), m_TotalHddSectors, m_FullLba48, m_Physical4k);
    stock.SetupPartitionsStock();
    return write_partition_tables(stock.GetPartitionTable());
}

XboxPartitionTable App::read_hdd_partition_table(LARGE_INTEGER offset)
{
    // Zeroed so a failed read gives a table without magic instead of stack garbage
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    LARGE_INTEGER RWOffset = offset;

    HANDLE hDisk = NULL;
    NTSTATUS status;
    ANSI_STRING a_path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    BYTE PartSector[HDD_SECTOR_SIZE];

    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    memset(PartSector, 0, sizeof(PartSector));

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_READ, &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
    if (status == 0)
    {
        status = NtReadFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset);
    }
    if (status == 0)
    {
        memcpy(&table, PartSector, HDD_SECTOR_SIZE);
    }
    if (hDisk != NULL)
    {
        NtClose(hDisk);
    }
    return table;
}

XboxPartitionTable App::read_hdd_main_partition_table()
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    if (m_TotalHddSectors > 0)
    {
        LARGE_INTEGER offset;
        offset.QuadPart = 0;
        table = read_hdd_partition_table(offset);
    }

    return table;
}

XboxPartitionTable App::read_hdd_backup_partition_table()
{
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));

    // No backup location, or no lba48 patch: then the reported disk size may be
    // the 28 bit one and the "last sector" is somewhere in the middle of the disk
    if (m_pTable->GetBackupTableLba() == 0 || m_pKernelTable == NULL)
    {
        m_BackupStatus = BACKUP_NOT_USED;
        return table;
    }

    // The table on disk (m_pTableOriginal: the kernel's, or the last one written)
    // decides which sectors hold partition data
    const bool backup_overlapped = m_pTableOriginal->OverlapsBackupTable();
    if (!backup_overlapped)
    {
        table = read_hdd_partition_table(get_backup_partition_offset_bytes());
        if (XboxPartitionTableManipulator::HasMagic(table))
        {
            m_BackupStatus = BACKUP_FOUND;
            return table;
        }
        memset(&table, 0, sizeof(table));
    }

    // Older XBpartitioner builds always put the backup in the last sector, also on
    // 512e drives where it now goes at the last 4K block. Read-only fallback so
    // those backups are still found; the next write puts it in the new place.
    m_BackupStatus = backup_overlapped ? BACKUP_OVERLAPPED : BACKUP_MISSING;
    const uint64_t last_sector_lba = m_pTable->GetLastSectorLba();
    if (last_sector_lba != m_pTable->GetBackupTableLba() && !m_pTableOriginal->OverlapsSector(last_sector_lba))
    {
        LARGE_INTEGER legacy_offset;
        legacy_offset.QuadPart = (ULONGLONG)last_sector_lba * (ULONGLONG)HDD_SECTOR_SIZE;
        table = read_hdd_partition_table(legacy_offset);
        if (XboxPartitionTableManipulator::HasMagic(table))
        {
            m_BackupStatus = BACKUP_FOUND_LEGACY;
        }
    }

    return table;
}

void __cdecl main()
{
    App app;
    app.MainLoop();
}

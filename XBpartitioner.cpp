// XBpartitioner.cpp : Defines the entry point for the application.
#include "stdafx.h"
#include "xbpartitioner.h"

#include <io.h>
#include <stdio.h> //used for swprintf
#include <stdlib.h>
#include <string.h> //used for wcscat
#include <winbase.h>
#include "Winioctl.h"
#include "prometheos_code.h"

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// Compile time checks (no static_assert in this compiler): an array of size -1
// fails the build. The partition table is read and written as one raw sector.
typedef char check_table_size_is_one_sector[(XboxPartitionTableSize == HDD_SECTOR_SIZE) ? 1 : -1];
typedef char check_table_struct_size[(sizeof(XboxPartitionTable) == XboxPartitionTableSize) ? 1 : -1];

//-----------------------------------------------------------------------------
// Screen layout, in 640x480 layout coordinates (see ui.h)
//-----------------------------------------------------------------------------

static const int MARGIN_X = 60;      // Left edge of messages and prompts
static const int LINE_HEIGHT = 20;

static const int TITLE_X = 52;
static const int TITLE_Y = 20;
static const int LOADED_TABLE_X = 400; // "Current table [...]", right of the title

static const int FREE_SPACE_Y = 394;     // Two lines: free GB, then free LBAs
static const int FORMAT_CLUSTER_X = 380; // "Will use N KB clusters", right of free GB
static const int STATUS_Y = 428;         // Status and result messages
static const int PROMPT_Y = 448;         // Prompts, and the Start action hint

// Partition table: standard partitions (1-5) in a box, then extended (6-14)
static const FLOAT TABLE_LEFT = 37.0f;
static const FLOAT TABLE_RIGHT = 603.0f;
static const int TABLE_LABEL_X = 40;
static const int STANDARD_LABEL_Y = 56;
static const FLOAT STANDARD_BOX_TOP = 80.0f;
static const FLOAT STANDARD_BOX_BOTTOM = 183.0f;
static const int EXTENDED_LABEL_Y = 188;
static const FLOAT EXTENDED_BOX_TOP = 210.0f;
static const FLOAT EXTENDED_BOX_BOTTOM = 392.0f;

static const int STANDARD_PARTITION_COUNT = 5;
static const int TABLE_FIRST_ROW_Y = 83;
static const int TABLE_EXTENDED_GAP = 30; // Extra space for the "Extended partitions" label
static const int TABLE_ROW_HEIGHT = 20;
static const int SELECTION_TOP = 3;       // Selection bar, relative to the row's text
static const int SELECTION_BOTTOM = 20;

// Columns
static const int COLUMN_INDEX_X = 50;
static const int COLUMN_START_X = 84;
static const int COLUMN_SEPARATOR_X = 230;
static const int COLUMN_SIZE_X = 238;
static const int COLUMN_CLUSTER_X = 400;
static const int COLUMN_BYTES_X = 592; // Right aligned

static const D3DCOLOR STANDARD_BOX_TOP_COLOR = D3DCOLOR_XRGB(0, 55, 55);
static const D3DCOLOR STANDARD_BOX_BOTTOM_COLOR = D3DCOLOR_XRGB(0, 130, 168);
static const D3DCOLOR EXTENDED_BOX_TOP_COLOR = D3DCOLOR_XRGB(155, 0, 168);
static const D3DCOLOR EXTENDED_BOX_BOTTOM_COLOR = D3DCOLOR_XRGB(51, 0, 55);
static const D3DCOLOR SELECTION_COLOR = D3DCOLOR_XRGB(51, 0, 55);

// Warning screens
static const int WARNING_TITLE_Y = 80;
static const int WARNING_TEXT_Y = 200;
static const int WARNING_LINE_HEIGHT = 40;

// Help screen
static const int HELP_BUTTON_X = 50;
static const int HELP_DESCRIPTION_X = 240;

// Step sizes for moving/resizing partitions, in sectors
static const ULONG FINE_STEP = 8;                                // Thumb stick: one 4KB block
static const ULONG DPAD_STEP = 0x200000;                         // D-pad: 1GB
static const ULONG TRIGGER_LIGHT_STEP = 64;                      // Barely pressed trigger
static const ULONG TRIGGER_FULL_STEP = SECTORS_PER_CLUSTER * 0x20000; // Fully pressed trigger: 2GB

//-----------------------------------------------------------------------------
// Startup and main loop
//-----------------------------------------------------------------------------

App::App()
    : m_pTable(NULL), m_pTableOriginal(NULL), m_pKernelTable(NULL),
      m_AnalogButton(-1), m_AnalogValue(0), m_DigitalButtons(0), m_LeftThumbY(0),
      m_FullLba48(false), m_TotalHddSectors(0), m_Physical4k(false), m_Lba48Version(0),
      m_CurrentPart(0), m_EditStart(false), m_StartAction(FUNCTION_WRITE_TABLE),
      m_LayoutScheme(0), m_LoadedTable(LOADED_TABLE_KERNEL)
{
    memset(m_ClusterSizes, 0, sizeof(m_ClusterSizes));
    memset(&m_TableKernel, 0, sizeof(m_TableKernel));
    memset(&m_TableHdd, 0, sizeof(m_TableHdd));
    memset(&m_TableHddBackup, 0, sizeof(m_TableHddBackup));
    memset(m_Controllers, 0, sizeof(m_Controllers));

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        RtlInitAnsiString(&m_PartitionPaths[i], partStr[i]);
    }

    m_Ui.Init();

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
    m_Physical4k = is_physical_4k_drive();

    // Read kernel patch info + kernel reported partition table.
    // This also creates m_pTableOriginal, the copy used to check if partitions changed.
    if (!reload_lba48() && !DisplayNoInfo())
    {
        QuitToDashboard();
    }

    // Read partition table from HDD
    m_TableHdd = read_hdd_main_partition_table();
    m_TableHddBackup = read_hdd_backup_partition_table();

    if (!XboxPartitionTableManipulator::HasMagic(m_TableHdd) && XboxPartitionTableManipulator::HasMagic(m_TableHddBackup))
    {
        DisplayWarningBackupAvailable();
    }

    CheckForV4TableOnOldBios();

    RefreshClusterSizes();

    m_CurrentPart = m_pTable->GetLastAvailablePartitionSlot();

    FadeIn();
}

App::~App()
{
    delete m_pTableOriginal;
    delete m_pTable;

    m_Ui.Shutdown();
}

void App::MainLoop()
{
    CheckTableStatus();

    bool wait_for_release = false;

    while (true)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawStartActionHint();
        m_Ui.EndFrame();

        // Buttons that act once per press are waited out, held ones repeat
        if (wait_for_release)
        {
            WaitForRelease();
        }

        XInputGetStateAny(&m_AnalogButton, &m_AnalogValue, &m_DigitalButtons, &m_LeftThumbY, 0);

        if (!m_DigitalButtons && !m_LeftThumbY)
        {
            wait_for_release = HandleAnalogButton(m_AnalogButton, m_AnalogValue);
        }
        else
        {
            wait_for_release = HandleDigitalButtons(m_DigitalButtons, m_LeftThumbY);
        }
    }
}

//-----------------------------------------------------------------------------
// Input handling
//-----------------------------------------------------------------------------

// A, B, X, Y, Black, White and the triggers
bool App::HandleAnalogButton(int button, int value)
{
    switch (button)
    {
    case XINPUT_GAMEPAD_A:
        CycleLayoutScheme();
        return true;

    case XINPUT_GAMEPAD_B:
        // No-op
        return true;

    case XINPUT_GAMEPAD_X:
        m_EditStart = !m_EditStart;
        return true;

    case XINPUT_GAMEPAD_Y:
        CycleLoadedTable();
        return true;

    case XINPUT_GAMEPAD_BLACK:
        if (ConfirmOnMainScreen(L"Partition table will be erased!"))
        {
            if (erase_partition_tables())
            {
                ShowStatus(L"Partition table erased.", COLOR_GREEN, true);
            }
            else
            {
                ShowStatus(L"Partition table erasure failed!", COLOR_RED, true);
            }
        }
        return false;

    case XINPUT_GAMEPAD_WHITE:
        m_StartAction = (m_StartAction + 1) % FUNCTION_COUNT;
        return true;

    case XINPUT_GAMEPAD_LEFT_TRIGGER:
        if (m_pTable->isPartitionEnabled(m_CurrentPart))
        {
            ChangeSelectedPartition(value, -1, false);
        }
        return false;

    case XINPUT_GAMEPAD_RIGHT_TRIGGER:
        if (!m_pTable->isPartitionEnabled(m_CurrentPart))
        {
            m_pTable->EnablePartition(m_CurrentPart);
        }
        else
        {
            ChangeSelectedPartition(value, 1, false);
        }
        return false;

    default:
        return false;
    }
}

// D-pad, thumb sticks and buttons, Start and Back
bool App::HandleDigitalButtons(int buttons, short left_thumb_y)
{
    if (left_thumb_y > 28671)
    {
        ChangeSelectedPartition(0, 1, true);
        return false;
    }

    if (left_thumb_y < -28672)
    {
        ChangeSelectedPartition(0, -1, true);
        return false;
    }

    if ((buttons & XINPUT_GAMEPAD_LEFT_THUMB) && (buttons & XINPUT_GAMEPAD_RIGHT_THUMB))
    {
        // Undo all changes since the last write
        m_pTable->LoadNewTable(m_pTableOriginal->GetPartitionTable());
        m_LoadedTable = LOADED_TABLE_KERNEL;
        m_CurrentPart = m_pTable->GetLastAvailablePartitionSlot();
        return true;
    }

    if (buttons & XINPUT_GAMEPAD_RIGHT_THUMB)
    {
        if (m_pTable->isPartitionEnabled(m_CurrentPart))
        {
            m_pTable->DisablePartition(m_CurrentPart);
            if (m_CurrentPart)
            {
                m_CurrentPart--;
            }
        }
        else
        {
            m_pTable->EnablePartition(m_CurrentPart);
        }
        return true;
    }

    const bool enabled = m_pTable->isPartitionEnabled(m_CurrentPart);

    if ((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) && enabled)
    {
        if (m_EditStart)
        {
            m_pTable->PartitionMoveForward(m_CurrentPart, DPAD_STEP);
        }
        else
        {
            m_pTable->PartitionIncreaseSizeBy(m_CurrentPart, DPAD_STEP);
        }
        Sleep(250);
    }
    else if ((buttons & XINPUT_GAMEPAD_DPAD_LEFT) && enabled)
    {
        if (m_EditStart)
        {
            m_pTable->PartitionMoveBack(m_CurrentPart, DPAD_STEP);
        }
        else
        {
            m_pTable->PartitionDecreaseSizeBy(m_CurrentPart, DPAD_STEP);
        }
        Sleep(250);
    }
    else if ((buttons & XINPUT_GAMEPAD_DPAD_UP) && m_CurrentPart > 0)
    {
        // Leaving an empty partition disables it
        if (!m_pTable->GetPartitionSize(m_CurrentPart))
        {
            m_pTable->DisablePartition(m_CurrentPart);
        }
        m_CurrentPart--;
        Sleep(150);
    }
    else if ((buttons & XINPUT_GAMEPAD_DPAD_DOWN) && m_CurrentPart < (XboxPartitionTableEntryCount - 1))
    {
        if (!m_pTable->GetPartitionSize(m_CurrentPart))
        {
            m_pTable->DisablePartition(m_CurrentPart);
        }
        m_CurrentPart++;
        Sleep(150);
    }
    else if (buttons & XINPUT_GAMEPAD_BACK)
    {
        DisplayHelp();
    }
    else if (buttons & XINPUT_GAMEPAD_START)
    {
        DoStart();
    }

    return false;
}

// A: stock -> F only -> F and G split -> stock ...
void App::CycleLayoutScheme()
{
    switch (m_LayoutScheme)
    {
    case 0:
        m_pTable->SetupPartitionsStock();
        break;
    case 1:
        m_pTable->SetupPartitionsFOnly();
        break;
    default:
        m_pTable->SetupPartitionsFGSplit();
        break;
    }

    m_LayoutScheme = (m_LayoutScheme + 1) % 3;
    m_LoadedTable = LOADED_TABLE_PRESET;
}

// Y: kernel -> hdd -> hdd backup -> kernel ... (after a preset: kernel first)
// Tables that are missing/unreadable (no magic) are skipped, loading those
// would wipe the layout. The kernel table is always valid, so this terminates.
void App::CycleLoadedTable()
{
    do
    {
        m_LoadedTable = (m_LoadedTable >= LOADED_TABLE_COUNT - 1) ? LOADED_TABLE_KERNEL : m_LoadedTable + 1;
    } while ((m_LoadedTable == LOADED_TABLE_HDD && !XboxPartitionTableManipulator::HasMagic(m_TableHdd)) ||
             (m_LoadedTable == LOADED_TABLE_HDD_BACKUP && !XboxPartitionTableManipulator::HasMagic(m_TableHddBackup)));

    switch (m_LoadedTable)
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
    }
}

// Resizes the selected partition, or moves its start when X has toggled that mode.
// trigger_value is the analog pressure (0-255), harder presses take bigger steps.
// fine (thumb stick) always uses the smallest step.
void App::ChangeSelectedPartition(int trigger_value, int direction, bool fine)
{
    ULONG lba_delta;

    if (fine)
    {
        lba_delta = FINE_STEP;
    }
    else if (trigger_value < 0x60)
    {
        lba_delta = TRIGGER_LIGHT_STEP;
        Sleep(250);
    }
    else if (trigger_value > 0xf0)
    {
        lba_delta = TRIGGER_FULL_STEP;
        Sleep(250);
    }
    else
    {
        // 0x60-0xf0: doubles with every 16 steps of pressure (256 sectors up to 256K sectors)
        lba_delta = (SECTORS_PER_CLUSTER * 2) << ((trigger_value >> 4) - 4);
    }

    if (m_EditStart)
    {
        if (direction > 0)
        {
            m_pTable->PartitionMoveForward(m_CurrentPart, lba_delta);
        }
        else
        {
            m_pTable->PartitionMoveBack(m_CurrentPart, lba_delta);
        }
    }
    else
    {
        if (direction > 0)
        {
            m_pTable->PartitionIncreaseSizeBy(m_CurrentPart, lba_delta);
        }
        else
        {
            m_pTable->PartitionDecreaseSizeBy(m_CurrentPart, lba_delta);
        }
    }
}

// Waits for a new button press. A button still held from the previous screen
// (e.g. Start, since the main loop polls without waiting) is released first,
// so it does not count as the answer.
void App::WaitForButton()
{
    WaitForRelease();
    XInputGetStateAny(&m_AnalogButton, &m_AnalogValue, &m_DigitalButtons, &m_LeftThumbY);
}

// Returns once nothing is pressed. Unlike XInputGetStateAny(wait = 2) this
// does not wait for a new press when everything is already released.
void App::WaitForRelease()
{
    while (XInputGetStateAny(&m_AnalogButton, &m_AnalogValue, &m_DigitalButtons, &m_LeftThumbY, 0) != -1)
    {
        Sleep(10);
    }
}

bool App::ConfirmWithY()
{
    WaitForButton();
    return m_AnalogButton == ANALOG_BUTTON_Y;
}

//-----------------------------------------------------------------------------
// Screens
//-----------------------------------------------------------------------------

// Everything on the main screen except the bottom two lines (status and prompt)
void App::DrawMainScreen()
{
    wchar_t title[100];
    swprintf(title, L"XBpartitioner 1.4 - LBA48 patch v%d", m_Lba48Version);
    m_Ui.Text(FONT_TEXT, title, TITLE_X, TITLE_Y, COLOR_WHITE);

    DrawLoadedTableName();
    DrawTable();
    DrawFreeSpace();
    DrawFormatCluster();
}

void App::DrawTable()
{
    m_Ui.Text(FONT_TEXT, L"Standard partitions. Custom sizes here might lead to problems", TABLE_LABEL_X, STANDARD_LABEL_Y, COLOR_WHITE);
    m_Ui.Rect(TABLE_LEFT, STANDARD_BOX_TOP, TABLE_RIGHT, STANDARD_BOX_BOTTOM, STANDARD_BOX_TOP_COLOR, STANDARD_BOX_BOTTOM_COLOR);

    m_Ui.Text(FONT_TEXT, L"Extended partitions", TABLE_LABEL_X, EXTENDED_LABEL_Y, COLOR_WHITE);
    m_Ui.Rect(TABLE_LEFT, EXTENDED_BOX_TOP, TABLE_RIGHT, EXTENDED_BOX_BOTTOM, EXTENDED_BOX_TOP_COLOR, EXTENDED_BOX_BOTTOM_COLOR);

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const int y = TABLE_FIRST_ROW_Y + i * TABLE_ROW_HEIGHT + (i >= STANDARD_PARTITION_COUNT ? TABLE_EXTENDED_GAP : 0);

        wchar_t partno_str[3];
        wchar_t lba_start_str[16];
        wchar_t lba_size_str[16];
        wchar_t cluster_str[16]; // Up to "4294967295KB"
        wchar_t size_str[20];
        const bool draw_cluster = PreparePartitionInfoStrings(i, partno_str, size_str, cluster_str, lba_start_str, lba_size_str);

        const Color text_color = m_pTable->isPartitionEnabled(i) ? COLOR_WHITE : COLOR_GREY;
        Color start_color = text_color;
        Color size_color = text_color;

        if (m_CurrentPart == i)
        {
            m_Ui.Rect(TABLE_LEFT, (FLOAT)(y - SELECTION_TOP), TABLE_RIGHT, (FLOAT)(y + SELECTION_BOTTOM), SELECTION_COLOR, SELECTION_COLOR);

            // Highlight the value the size controls change
            if (m_EditStart)
            {
                start_color = COLOR_YELLOW;
            }
            else
            {
                size_color = COLOR_YELLOW;
            }
        }

        m_Ui.Text(FONT_TABLE, partno_str, COLUMN_INDEX_X, y, text_color);
        m_Ui.Text(FONT_TABLE, lba_start_str, COLUMN_START_X, y, start_color);
        m_Ui.Text(FONT_TEXT, L":", COLUMN_SEPARATOR_X, y, text_color);
        m_Ui.Text(FONT_TABLE, lba_size_str, COLUMN_SIZE_X, y, size_color);
        if (draw_cluster)
        {
            m_Ui.Text(FONT_TABLE, cluster_str, COLUMN_CLUSTER_X, y, text_color);
        }
        m_Ui.Text(FONT_TABLE, size_str, COLUMN_BYTES_X, y, text_color, XFONT_RIGHT);
    }
}

// Formats one table row. Returns true if the cluster column should be drawn.
bool App::PreparePartitionInfoStrings(const int partition_number, wchar_t *partno_str, wchar_t *size_string, wchar_t *cluster_string, wchar_t *lba_start_str, wchar_t *lba_size_str)
{
    const ULONGLONG lba_start = m_pTable->GetPartitionStart(partition_number);
    const ULONGLONG lba_size = m_pTable->GetPartitionSize(partition_number);

    swprintf(partno_str, L"%d", partition_number + 1);
    // 48 bit LBAs: 4 high + 8 low hex digits
    swprintf(lba_start_str, L"%.04X%.08X", (ULONG)(WORD)(lba_start >> 32), (ULONG)(lba_start & LBA_LOWER_BITS));
    swprintf(lba_size_str, L"%.04X%.08X", (ULONG)(WORD)(lba_size >> 32), (ULONG)(lba_size & LBA_LOWER_BITS));

    // Extended partitions in GB, the small standard ones in MB
    const ULONGLONG size_bytes = m_pTable->GetPartitionSizeInBytes(partition_number);
    if (partition_number >= STANDARD_PARTITION_COUNT)
    {
        swprintf(size_string, L"%.2f GB", (double)size_bytes / SCALE_GIGABYTE);
    }
    else
    {
        swprintf(size_string, L"%.2f MB", (double)size_bytes / SCALE_MEGABYTE);
    }

    const unsigned int cluster_size = m_ClusterSizes[partition_number];

    if (cluster_size == 0)
    {
        swprintf(cluster_string, L"");
    }
    else if (cluster_size < m_pTable->CalculateClusterSize(lba_size))
    {
        // Too small for the partition size, uses the same rule as the formatter
        swprintf(cluster_string, L"ERROR");
    }
    else
    {
        swprintf(cluster_string, L"%dKB", cluster_size);
    }

    return (cluster_size != 0 && lba_size > 0);
}

void App::DrawFreeSpace()
{
    const uint64_t usable_lbas = m_pTable->GetUsableLBAs();
    const uint64_t available_lbas = m_pTable->GetAvailableLBAs();

    wchar_t free_gb_str[150];
    wchar_t free_lba_str[150];

    // 0x200000 sectors per GB. %I64u: this CRT does not support %llu.
    swprintf(free_gb_str, L"Usable free space: %.0f of %.0f GB", (float)available_lbas / 0x200000, (float)usable_lbas / 0x200000);
    swprintf(free_lba_str, L"Usable LBAs: %I64u, available LBAs %I64u", usable_lbas, available_lbas);
    m_Ui.Text(FONT_TEXT, free_gb_str, MARGIN_X, FREE_SPACE_Y, COLOR_WHITE);
    m_Ui.Text(FONT_TEXT, free_lba_str, MARGIN_X, FREE_SPACE_Y + LINE_HEIGHT, COLOR_WHITE);
}

void App::DrawFormatCluster()
{
    if (m_StartAction == FUNCTION_FORMAT_CURRENT_PARTITION)
    {
        wchar_t cluster_str[50];
        swprintf(cluster_str, L"Will use %d KB clusters", m_pTable->CalculateClusterSizeForPartition(m_CurrentPart));
        m_Ui.Text(FONT_TEXT, cluster_str, FORMAT_CLUSTER_X, FREE_SPACE_Y, COLOR_WHITE);
    }
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
        m_Ui.Text(FONT_TEXT, names[m_LoadedTable], LOADED_TABLE_X, TITLE_Y, COLOR_WHITE);
    }
}

void App::DrawStartActionHint()
{
    // Indexed by Function
    static const LPCWSTR hints[FUNCTION_COUNT] = {
        L"Press Start to write partition table. Press Back for help.",
        L"Press Start to write table and format. Press Back for help.",
        L"Press Start to format current partition. Press Back for help.",
    };

    if (m_StartAction >= 0 && m_StartAction < FUNCTION_COUNT)
    {
        m_Ui.Text(FONT_TEXT, hints[m_StartAction], MARGIN_X, PROMPT_Y, COLOR_WHITE);
    }
}

// Draws "Press Y <action>" with a yellow Y
void App::DrawYPrompt(LPCWSTR action, int x, int y)
{
    m_Ui.Text(FONT_TEXT, L"Press", x, y, COLOR_LIGHT_GREY);
    m_Ui.Text(FONT_TEXT, L"Y", x + 50, y, COLOR_YELLOW);
    m_Ui.Text(FONT_TEXT, action, x + 68, y, COLOR_LIGHT_GREY);
}

// Full screen warning: red title, then one line of text per entry.
// If y_prompt_action is set a "Press Y ..." line is added at the end.
// Draws and presents, does not wait for input.
void App::DrawWarningScreen(const LPCWSTR *lines, int line_count, LPCWSTR y_prompt_action)
{
    m_Ui.BeginFrame();
    m_Ui.Text(FONT_TITLE, L"!!!!!!!! WARNING !!!!!!!!", LAYOUT_WIDTH / 2, WARNING_TITLE_Y, COLOR_WARNING, XFONT_CENTER);

    int y = WARNING_TEXT_Y;
    for (int i = 0; i < line_count; i++, y += WARNING_LINE_HEIGHT)
    {
        m_Ui.Text(FONT_TEXT, lines[i], MARGIN_X, y, COLOR_LIGHT_GREY);
    }

    if (y_prompt_action != NULL)
    {
        DrawYPrompt(y_prompt_action, MARGIN_X, y);
    }

    m_Ui.EndFrame();
}

void App::FadeIn()
{
    for (int alpha = 0xff; alpha > 0x06; alpha -= 0x06)
    {
        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawStartActionHint();
        m_Ui.FillScreen(D3DCOLOR_ARGB(alpha, 0, 0, 0));
        m_Ui.EndFrame();
    }
}

// Main screen with a message in the status line.
// With wait_for_button, adds "Press any button" and blocks until one is pressed.
void App::ShowStatus(LPCWSTR message, Color color, bool wait_for_button)
{
    m_Ui.BeginFrame();
    DrawMainScreen();
    m_Ui.Text(FONT_TEXT, message, MARGIN_X, STATUS_Y, color);
    if (wait_for_button)
    {
        m_Ui.Text(FONT_TEXT, L"Press any button to continue.", MARGIN_X, PROMPT_Y, COLOR_WHITE);
    }
    m_Ui.EndFrame();

    if (wait_for_button)
    {
        WaitForButton();
    }
}

// Main screen with a message and a "Press Y to continue" prompt, returns true on Y
bool App::ConfirmOnMainScreen(LPCWSTR message)
{
    m_Ui.BeginFrame();
    DrawMainScreen();
    m_Ui.Text(FONT_TEXT, message, MARGIN_X, STATUS_Y, COLOR_WHITE);
    DrawYPrompt(L"to continue or any other button to abort.", MARGIN_X, PROMPT_Y);
    m_Ui.EndFrame();

    return ConfirmWithY();
}

struct HelpEntry
{
    LPCWSTR button;
    Color color;
    LPCWSTR description;
};

static const HelpEntry g_HelpEntries[] = {
    {L"A", COLOR_GREEN, L"cycle through standard setup schemes"},
    {L"B", COLOR_RED, L"does nothing"},
    {L"X", COLOR_BLUE, L"toggle change size \\ change start sector"},
    {L"Y", COLOR_YELLOW, L"cycle kernel \\ hdd \\ hdd backup partition tables"},
    {L"L Trigger", COLOR_YELLOW, L"decrease partition size"},
    {L"R Trigger", COLOR_GREEN, L"increase partition size"},
    {L"D-Pad Left", COLOR_RED, L"decrease partition size by 1GB"},
    {L"D-Pad Right", COLOR_BLUE, L"increase partition size by 1GB"},
    {L"D-Pad Up/Down", COLOR_YELLOW, L"select partition"},
    {L"L Thumb Stick", COLOR_YELLOW, L"partition size fine control"},
    {L"R Thumb Button", COLOR_GREEN, L"enable \\ disable current partition"},
    {L"L+R Thumb Buttons", COLOR_RED, L"undo all changes since last write"},
    {L"Black", COLOR_BLUE, L"erase the partition table from the disk"},
    {L"White", COLOR_YELLOW, L"select Start Button action"},
    {L"Start", COLOR_GREEN, L"write & format changed \\ write only \\ format"},
};

void App::DisplayHelp()
{
    m_Ui.BeginFrame();

    int y = LINE_HEIGHT;
    wchar_t title[80];
    swprintf(title, L"XBpartitioner Help (video: %s, hdd: %s sectors)", m_Ui.GetVideoModeName(), m_Physical4k ? L"4K" : L"512");
    m_Ui.Text(FONT_TEXT, title, LAYOUT_WIDTH / 2, y, COLOR_WHITE, XFONT_CENTER);
    y += 2 * LINE_HEIGHT;

    for (int i = 0; i < ARRAY_COUNT(g_HelpEntries); i++, y += LINE_HEIGHT)
    {
        m_Ui.Text(FONT_TEXT, g_HelpEntries[i].button, HELP_BUTTON_X, y, g_HelpEntries[i].color);
        m_Ui.Text(FONT_TEXT, g_HelpEntries[i].description, HELP_DESCRIPTION_X, y, COLOR_WHITE);
    }

    y += LINE_HEIGHT + 4;
    m_Ui.Text(FONT_TEXT, L"Actions which write to disk require confirmation.", HELP_BUTTON_X, y, COLOR_YELLOW);
    y += LINE_HEIGHT;
    m_Ui.Text(FONT_TEXT, L"Press Back to exit XBpartitioner, any other button to go back.", HELP_BUTTON_X, y, COLOR_YELLOW);

    m_Ui.EndFrame();

    WaitForButton();

    if (m_DigitalButtons & XINPUT_GAMEPAD_BACK)
    {
        QuitToDashboard();
    }
}

void App::DisplayWarningBackupAvailable()
{
    static const LPCWSTR lines[] = {
        L"Main partition table is missing, however backup is available",
        L"Press Y to load the backup table.",
        L"Press any button to continue...",
    };
    DrawWarningScreen(lines, ARRAY_COUNT(lines), NULL);
    WaitForButton();
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
    wchar_t line2[80];
    swprintf(line1, L"This BIOS has LBA48 patch v%d, but a partition table", m_Lba48Version);
    swprintf(line2, L"uses v4 (over 2TB) values. Found in: %s.", found_in);

    const LPCWSTR lines[] = {
        line1,
        line2,
        L"It is likely a v4 table: partitions above 2TB are shown wrong.",
        L"Do not write or format. Use a v4 BIOS such as Cerbios.",
        L"Press any button to continue...",
    };
    DrawWarningScreen(lines, ARRAY_COUNT(lines), NULL);
    WaitForButton();
}

// Returns true if the user confirmed formatting with Y
bool App::ConfirmFormatWarning()
{
    wchar_t message[50];
    if (m_StartAction == FUNCTION_WRITE_TABLE_FORMAT)
    {
        swprintf(message, L"Changed partitions will be formatted!");
    }
    else
    {
        swprintf(message, L"Partition %d will be formatted!", m_CurrentPart + 1);
    }

    const LPCWSTR lines[] = {message};
    DrawWarningScreen(lines, ARRAY_COUNT(lines), L"to continue or any other button to abort.");
    return ConfirmWithY();
}

// Returns true if the user chose to continue without lba48 info
bool App::DisplayNoInfo()
{
    static const LPCWSTR lines[] = {
        L"LBA48 info not found! Custom Partitions will not work with this BIOS!",
        L"Update your BIOS to Cerbios.",
    };
    DrawWarningScreen(lines, ARRAY_COUNT(lines), L"to continue or any other button to quit.");
    return ConfirmWithY();
}

void App::CheckTableStatus()
{
    // Check if we should report that main and backup dont match up.
    // Only when both are valid: a missing main is reported by DisplayWarningBackupAvailable(),
    // and a missing backup is normal for drives partitioned with other tools.
    if (XboxPartitionTableManipulator::HasMagic(m_TableHdd) &&
        XboxPartitionTableManipulator::HasMagic(m_TableHddBackup) &&
        memcmp(&m_TableHdd, &m_TableHddBackup, sizeof(XboxPartitionTable)) != 0)
    {
        static const LPCWSTR lines[] = {
            L"Main and backup partition tables dont match!",
            L"Carefully check which one is correct before doing anything!",
            L"Press Y to cycle kernel \\ hdd \\ hdd backup tables.",
            L"Press any button to continue...",
        };
        DrawWarningScreen(lines, ARRAY_COUNT(lines), NULL);
        WaitForButton();
    }
}

// Fades the current screen to black and returns to the dashboard. Does not return.
void App::QuitToDashboard()
{
    m_Ui.FadeOut();
    m_Ui.Shutdown();

    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};
    XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
}

//-----------------------------------------------------------------------------
// Actions bound to Start
//-----------------------------------------------------------------------------

void App::DoFormatAll()
{
    if (!CheckTableFitsBeforeBackup())
    {
        return;
    }

    if (!ConfirmFormatWarning())
    {
        return;
    }

    XboxPartitionTable table = m_pTable->GetPartitionTable();

    // write_partition_tables() reloads both manipulators from the new table,
    // so keep a copy of the old one to find out which partitions changed
    const XboxPartitionTableManipulator original(m_pTableOriginal->GetPartitionTable(), m_TotalHddSectors, m_FullLba48, m_Physical4k);

    if (!write_partition_tables(table))
    {
        ShowStatus(L"Partition table write failed!", COLOR_RED, true);
        return;
    }

    // Without the lba48 patch the kernel keeps using its old partition layout
    // until a reboot, so formatting now would hit the wrong sectors
    if (!m_pKernelTable)
    {
        ShowStatus(L"Table written, not formatted: formatting needs an LBA48 BIOS.", COLOR_YELLOW, true);
        return;
    }

    ShowStatus(L"Partition table written...", COLOR_GREEN, false);
    Sleep(500);

    // Format modified partitions
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        // Check if partition actually changed
        if (m_pTable->isPartitionEnabled(i) &&
            (original.GetPartitionSize(i) != m_pTable->GetPartitionSize(i) ||
             original.GetPartitionStart(i) != m_pTable->GetPartitionStart(i)))
        {
            wchar_t format_str[50];
            swprintf(format_str, L"Formatting partition %d with %dk clusters...", i + 1, m_pTable->CalculateClusterSizeForPartition(i));
            ShowStatus(format_str, COLOR_GREEN, false);
            format_partition(i);
        }
    }

    // Update the original table since we saved a new copy
    m_pTableOriginal->LoadNewTable(table);
}

// Shows why the table can't be written if a partition reaches the backup table sector
bool App::CheckTableFitsBeforeBackup()
{
    if (!m_pTable->OverlapsBackupTable())
    {
        return true;
    }

    static const LPCWSTR lines[] = {
        L"The last partition runs into the backup partition table.",
        L"On 4K sector drives the backup is in the last 4K block.",
        L"Shrink the last partition a little, then write again.",
        L"Press any button to continue...",
    };
    DrawWarningScreen(lines, ARRAY_COUNT(lines), NULL);
    WaitForButton();
    return false;
}

void App::DoWriteTable()
{
    if (!CheckTableFitsBeforeBackup())
    {
        return;
    }

    if (!ConfirmOnMainScreen(L"Partition table will be written to hard drive!"))
    {
        return;
    }

    if (write_partition_tables(m_pTable->GetPartitionTable()))
    {
        ShowStatus(L"Partition table written...", COLOR_GREEN, true);
    }
    else
    {
        ShowStatus(L"Partition table write failed!", COLOR_RED, true);
    }
}

void App::DoFormatOne()
{
    if (!ConfirmFormatWarning())
    {
        return;
    }

    // Update kernel partition table if changed, else we might format a partition with wrong start/size
    if (CheckIfPartitionTableChanged())
    {
        if (!m_pKernelTable)
        {
            // Can't tell the kernel about the new layout, it would format the old sectors
            ShowStatus(L"Table changed: formatting it needs an LBA48 BIOS.", COLOR_YELLOW, true);
            return;
        }

        XboxPartitionTable table = m_pTable->GetPartitionTable();
        memcpy(m_pKernelTable, &table, sizeof(XboxPartitionTable));
        RefreshClusterSizes();
    }

    wchar_t format_str[50];
    swprintf(format_str, L"Formatting partition %d with %dk clusters...", m_CurrentPart + 1, m_pTable->CalculateClusterSizeForPartition(m_CurrentPart));
    ShowStatus(format_str, COLOR_GREEN, false);

    format_partition(m_CurrentPart);
}

void App::DoStart()
{
    switch (m_StartAction)
    {
    case FUNCTION_WRITE_TABLE:
        DoWriteTable();
        break;
    case FUNCTION_WRITE_TABLE_FORMAT:
        DoFormatAll();
        break;
    case FUNCTION_FORMAT_CURRENT_PARTITION:
        if (m_pTable->isPartitionEnabled(m_CurrentPart))
        {
            DoFormatOne();
        }
        break;
    default:
        break;
    }
}

bool App::format_partition(const int i)
{
    const int cluster_size = m_pTable->CalculateClusterSizeForPartition(i);
    const ULONG cluster_size_bytes = cluster_size << 10;

    // XapiFormatFATVolumeEx returns a BOOL (TRUE on success), not an NTSTATUS
    const bool formatted = XapiFormatFATVolumeEx(&m_PartitionPaths[i], cluster_size_bytes) != FALSE;
    const bool fixed_up = formatted && largePartitionFixup(&m_PartitionPaths[i], cluster_size_bytes);

    // The FATX header changed (or may be half written), re-read what is on disk
    m_ClusterSizes[i] = get_cluster_size_from_partition(m_PartitionPaths[i]);

    if (!formatted)
    {
        ShowStatus(L"Failed to format partition...", COLOR_RED, true);
        return false;
    }

    if (!fixed_up)
    {
        ShowStatus(L"Failed to fix up big partition...", COLOR_RED, true);
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Controller polling
//-----------------------------------------------------------------------------

// Reads the first controller with something pressed.
// wait: 0 = poll once, 1 = wait for a press, 2 = wait for a press and its release.
// Returns the controller index, or -1 if nothing was pressed (wait = 0).
int App::XInputGetStateAny(int *analog, int *analog_val, int *buttons, short *l_thumb_y_val, int wait)
{
    XINPUT_STATE ControllerState[4];
    int i, j, c = -1;
    *analog = -1;
    *buttons = 0;
    *l_thumb_y_val = 0;

    do
    {
        for (i = 0; i < 4; i++)
        {
            if (m_Controllers[i])
            {
                XInputGetState(m_Controllers[i], &ControllerState[i]);
                *buttons = ControllerState[i].Gamepad.wButtons;

                if (*buttons)
                {
                    c = i;
                    break;
                }

                if (!wait && (ControllerState[i].Gamepad.sThumbLY < -28672 || ControllerState[i].Gamepad.sThumbLY > 28671))
                {
                    *l_thumb_y_val = ControllerState[i].Gamepad.sThumbLY;
                    c = i;
                    break;
                }

                for (j = 0; j < 8; j++)
                {
                    if (ControllerState[i].Gamepad.bAnalogButtons[j])
                    {
                        *analog_val = ControllerState[i].Gamepad.bAnalogButtons[j];
                        *analog = j;
                        c = i;
                        break;
                    }
                }

                // Stop at the first controller with something pressed, the next
                // controller would overwrite *buttons
                if (c != -1)
                {
                    break;
                }
            }
        }
        if (!wait)
        {
            return c;
        }
    } while (c == -1);

    for (; wait == 2;)
    {
        XInputGetState(m_Controllers[c], &ControllerState[c]);

        // *analog is -1 when a digital button was pressed
        if (!ControllerState[c].Gamepad.wButtons && (*analog < 0 || !ControllerState[c].Gamepad.bAnalogButtons[*analog]))
        {
            break;
        }
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
// IDENTIFY DEVICE word 106: physical/logical sector size
#define ATA_ID_SECTOR_SIZE_WORD 106

// True if the drive has 4K (or larger) physical sectors behind 512 byte logical
// ones (512e / Advanced Format). Reported by ATA IDENTIFY DEVICE word 106:
//   bits 15:14 = 01  word is valid
//   bit 13         multiple logical sectors per physical sector
//   bits 3:0       log2(logical sectors per physical sector), 3 = 8 = 4K
// Any failure is treated as a plain 512 byte drive.
bool App::is_physical_4k_drive()
{
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
        return false;
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
        return false;
    }

    const USHORT sector_size_info = identify[ATA_ID_SECTOR_SIZE_WORD];
    if ((sector_size_info & 0xC000) != 0x4000)
    {
        // Word not supported, old drive: 512 byte physical sectors
        return false;
    }

    return (sector_size_info & 0x2000) != 0 && (sector_size_info & 0x000F) >= 3;
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

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);

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

bool App::CheckIfPartitionTableChanged()
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        ULONGLONG new_partition_start = m_pTable->GetPartitionStart(i);
        ULONGLONG new_partition_size = m_pTable->GetPartitionSize(i);

        ULONGLONG existing_partition_start = m_pTableOriginal->GetPartitionStart(i);
        ULONGLONG existing_partition_size = m_pTableOriginal->GetPartitionSize(i);

        // Check if partition actually changed
        if (m_pTable->isPartitionEnabled(i) && (existing_partition_size != new_partition_size || existing_partition_start != new_partition_start))
        {
            return true;
        }
    }
    return false;
}

LARGE_INTEGER App::get_backup_partition_offset_bytes()
{
    // The manipulator decides where the backup goes (last sector, or the last 4K
    // block on 512e drives, capped for pre v4 lba48) and keeps partitions clear of it
    LARGE_INTEGER byte_pos;
    byte_pos.QuadPart = (ULONGLONG)m_pTable->GetBackupTableLba() * (ULONGLONG)HDD_SECTOR_SIZE;
    return byte_pos;
}

bool App::write_partition_tables(const XboxPartitionTable table)
{
    // Without a known disk size there is no valid backup location
    if (m_pTable->GetBackupTableLba() == 0)
    {
        return false;
    }

    // Never write the backup over the end of a partition (callers check
    // CheckTableFitsBeforeBackup() first to explain why)
    const XboxPartitionTableManipulator to_write(table, m_TotalHddSectors, m_FullLba48, m_Physical4k);
    if (to_write.OverlapsBackupTable())
    {
        return false;
    }

    LARGE_INTEGER RWOffset;
    RWOffset.QuadPart = 0;
    LARGE_INTEGER RWOffset_backup = get_backup_partition_offset_bytes();

    BYTE PartSector[HDD_SECTOR_SIZE]; // XboxPartitionTable should be the same size as HDD_SECTOR_SIZE
    memcpy(PartSector, &table, HDD_SECTOR_SIZE);

    HANDLE hDisk = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ANSI_STRING a_path;
    RtlInitAnsiString(&a_path, "\\Device\\Harddisk0\\Partition0");
    InitializeObjectAttributes(&ObjectAttributes, &a_path, OBJ_CASE_INSENSITIVE, NULL);
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);

    // Backup first: if it fails nothing has been touched yet. If the main write then
    // fails, the mismatch check on the next boot will point it out.
    if (status == 0)
    {
        // Write backup table
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset_backup);
    }

    if (status == 0)
    {
        // Write main table
        status = NtWriteFile(hDisk, NULL, NULL, NULL, &IoStatusBlock, (PVOID)PartSector, HDD_SECTOR_SIZE, &RWOffset);
    }

    // This shouldn't really be here, but both the kernel and the internal XBpartitioner logic need to know about the new partition table
    if (status == 0 && m_pKernelTable)
    {
        // p_table_add
        memcpy(m_pKernelTable, &table, sizeof(XboxPartitionTable));
        memcpy(&m_TableKernel, &table, sizeof(XboxPartitionTable));
        if (m_pTable)
        {
            m_pTable->LoadNewTable(table);
        }
    }

    if (hDisk != NULL)
    {
        NtClose(hDisk);
    }

    if (status != 0)
    {
        return false;
    }

    // Refresh the cached on-disk copies, otherwise cycling with Y loads stale tables
    m_TableHdd = read_hdd_main_partition_table();
    m_TableHddBackup = read_hdd_backup_partition_table();

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

    // The editor now holds what was written, which is also the kernel's table
    m_LoadedTable = LOADED_TABLE_KERNEL;

    // Partitions may now point at different sectors
    RefreshClusterSizes();
    return true;
}

bool App::erase_partition_tables()
{
    m_pTable->SetupPartitionsStock();
    return write_partition_tables(m_pTable->GetPartitionTable());
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

    status = NtOpenFile(&hDisk, SYNCHRONIZE | GENERIC_ALL, &ObjectAttributes, &IoStatusBlock, 0, FILE_SYNCHRONOUS_IO_ALERT);
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

    if (m_pTable->GetBackupTableLba() == 0)
    {
        return table;
    }

    table = read_hdd_partition_table(get_backup_partition_offset_bytes());

    // Older XBpartitioner builds always put the backup in the last sector, also on
    // 512e drives where it now goes at the last 4K block. Read-only fallback so
    // those backups are still found; the next write puts it in the new place.
    const uint64_t last_sector_lba = m_pTable->GetLastSectorLba();
    if (!XboxPartitionTableManipulator::HasMagic(table) && last_sector_lba != m_pTable->GetBackupTableLba())
    {
        LARGE_INTEGER legacy_offset;
        legacy_offset.QuadPart = (ULONGLONG)last_sector_lba * (ULONGLONG)HDD_SECTOR_SIZE;
        table = read_hdd_partition_table(legacy_offset);
    }

    return table;
}

//-----------------------------------------------------------------------------
// Name: main()
// Desc: The application's entry point
//-----------------------------------------------------------------------------
void __cdecl main()
{
    App app;
    app.MainLoop();
}

#ifndef __XBPARTITIONER_H__
#define __XBPARTITIONER_H__

#include <Xtl.h>
#include "partition.hpp"
#include "Undocumented.h"
#include "ui.h"

const char partStr[][32] = {
    "\\Device\\Harddisk0\\Partition1",
    "\\Device\\Harddisk0\\Partition2",
    "\\Device\\Harddisk0\\Partition3",
    "\\Device\\Harddisk0\\Partition4",
    "\\Device\\Harddisk0\\Partition5",
    "\\Device\\Harddisk0\\Partition6",
    "\\Device\\Harddisk0\\Partition7",
    "\\Device\\Harddisk0\\Partition8",
    "\\Device\\Harddisk0\\Partition9",
    "\\Device\\Harddisk0\\Partition10",
    "\\Device\\Harddisk0\\Partition11",
    "\\Device\\Harddisk0\\Partition12",
    "\\Device\\Harddisk0\\Partition13",
    "\\Device\\Harddisk0\\Partition14"
};

// Which partition table is loaded in the editor: picked in Start > Partition table,
// or a preset layout from A on the main screen.
enum LoadedTable
{
    LOADED_TABLE_KERNEL = 0,
    LOADED_TABLE_HDD = 1,
    LOADED_TABLE_HDD_BACKUP = 2,
    LOADED_TABLE_COUNT, // Number of tables that can be loaded
    LOADED_TABLE_PRESET = LOADED_TABLE_COUNT
};

// What the drive reports about itself (ATA IDENTIFY DEVICE)
struct DriveInfo
{
    bool identify_ok;              // false: IDENTIFY failed, the rest is empty
    wchar_t model[41];
    wchar_t firmware[9];
    ULONGLONG native_sectors;      // The drive's own size, independent of the BIOS
    unsigned int physical_sector_size;
    bool physical_4k;              // 512e drive
};

// Where the backup partition table was found when it was last read
enum BackupStatus
{
    BACKUP_NOT_USED = 0,  // No backup location: no lba48 patch, unknown size, or stock 8GB drive
    BACKUP_MISSING,       // Location known but no valid table there
    BACKUP_FOUND,         // Valid table at the expected location
    BACKUP_FOUND_LEGACY,  // Valid table at the old location (last sector) on a 512e drive
    BACKUP_OVERLAPPED     // The last partition on disk covers the backup sector: not read, not written
};

// RunMenu() results besides the selected item
#define MENU_RESULT_BACK -1  // B: back one level
#define MENU_RESULT_CLOSE -2 // Start: close all menus


// Screen layout in real screen pixels, one per video mode (g_Layout480,
// g_Layout720 in XBpartitioner.cpp). Both describe a 4:3 area; the renderer
// centers it on the screen (720p: 960x720 with side bars) and draws 1:1, so
// nothing is scaled or rounded.
struct Layout
{
    int width;  // 4:3 area
    int height;

    // Main screen
    int margin_x; // Left edge of messages and prompts
    int line_height;
    int title_x;
    int title_y;
    int loaded_table_x; // "Current table [...]", right of the title
    int free_space_y;   // "Free: ... of ...", under the legend
    int status_y;       // Progress messages
    int prompt_y;       // Controls hint

    // Partition table: one menu style panel. Its rows ("slots", all
    // table_row_height apart) are the column headers, a line, "Standard
    // partitions", partitions 1-5, "Extended partitions", partitions 6-14.
    int table_left;
    int table_right;
    int table_top;          // Panel top; the same space is left below the last row
    int table_first_row_y;  // Top of the header row's bar
    int table_header_gap;   // Extra space after the header row, for the line
    int table_row_height;   // Row pitch
    int table_row_padding;  // Above and below the text in a row's selection bar

    // Legend for the table's markers, right aligned under the table
    int legend_y;
    // Disk map above the table: the partitions to scale across the whole drive
    int map_y;
    int map_height;

    // Dialog boxes, centered vertically
    int dialog_left;
    int dialog_right;
    int dialog_text_inset;
    int dialog_title_offset; // Title text from the top of the box
    int dialog_title_height;
    int dialog_line_height;
    int dialog_button_width;
    int dialog_button_padding; // Above and below the label: button height = text + 2x this
    int dialog_button_gap;
    int dialog_padding;

    // Menus
    int menu_left;
    int menu_right;
    int menu_top;
    int menu_title_offset;      // Title text from the top of the box
    int menu_first_item_offset; // First item slot from the top of the box
    int menu_item_padding;      // Above and below the text: bar height = text + 2x this
    int menu_item_gap;          // Space between two selection bars
    int menu_text_inset;        // Item text from the left of the box
    int menu_selection_inset;   // Selection bar from the sides of the box
    int menu_footer_height;
    int menu_footer_text_offset;

    // System information: a wider menu panel, rows spaced like menu items
    int info_left;
    int info_right;
    int info_value_offset; // Values from the panel's left edge (labels use menu_text_inset)

};

// Up/down repeat for a held D-pad, timed rather than slept so screens keep
// drawing (and selection bars keep sliding) while it is held. Step() is called
// once per frame with the direction held (-1 up, 1 down, 0 none) and returns
// the direction to move this frame, or 0: a new press moves at once, a held one
// after REPEAT_DELAY_MS and then every REPEAT_RATE_MS.
struct DpadRepeat
{
    int held;
    DWORD last_move;
    bool repeating;

    DpadRepeat() : held(0), last_move(0), repeating(false) {}
    int Step(int direction);
};

// Moves a sliding selection bar part of the way to its target, once per frame:
// it eases in and settles exactly on the target
// last_tick: when it last moved (GetTickCount), 0 at first. The move is time
// based, so the bar is as fast when frames take longer to draw.
void SlideTowards(FLOAT &position, FLOAT target, DWORD &last_tick);

class App
{
public:
    App();
    ~App();

    void MainLoop();

private:
    // Screens. All coordinates are in layout space, see ui.h.
    void DrawMainScreen();
    // The main screen behind a menu, dialog or the editor: drawn and saved when
    // dirty (then cleared), else the saved copy (Renderer::SaveBackBuffer)
    void DrawBackdrop(bool &dirty);
    void DrawTable();
    void DrawDiskMap();
    void FormatTableHeader(wchar_t *row);
    void DrawFreeSpace();
    void DrawLoadedTableName();
    void DrawControlsHint();
    bool RunDialog(LPCWSTR title, Color title_color, const LPCWSTR *lines, int line_count, LPCWSTR ok_label, LPCWSTR cancel_label);
    bool ConfirmWarning(const LPCWSTR *lines, int line_count, LPCWSTR ok_label);
    void ShowWarning(const LPCWSTR *lines, int line_count);
    void FadeIn();

    void ShowStatus(LPCWSTR message, Color color);
    void ShowMessage(LPCWSTR title, Color title_color, LPCWSTR message);
    void DisplaySystemInfo();
    bool SystemInfoNeedsAttention();
    void OfferBackupRestore();
    bool IsMainTableUsable();
    bool IsBackupTableUsable();
    void CheckForV4TableOnOldBios();
    bool ConfirmFormatWarning(bool format_changed);
    bool DisplayNoInfo();
    void CheckTableStatus();
    void RunStartupChecks();
    bool ConfirmExit();
    void QuitToDashboard();

    void FormatPartitionRow(const int partition_number, wchar_t *row);

    // Input
    int XInputGetStateAny(int *analog, int *buttons, int wait = 2);
    void WaitForButton();
    void WaitForRelease();
    void HandleMainScreenButtons(int buttons, int analog);
    void UpdateSelectionBar();
    void ApplyPreset(int preset);
    void UndoAllChanges();

    // Start menu and its actions
    void WarnBackupOverlapped();
    // Called by RunMenu with the selected item, at the start and whenever it changes
    typedef void (App::*MenuPreview)(int item);
    int RunMenu(LPCWSTR title, const LPCWSTR *labels, const bool *enabled, int count, int initial = 0, bool submenu = false,
                MenuPreview preview = NULL);
    // Colors of the current theme
    const Theme &Colors() const { return m_Ui.GetTheme(); }
    bool DisplayThemeMenu();
    void PreviewTheme(int index) { m_Ui.SetTheme(index); }
    void LoadThemeSetting();
    bool SaveThemeSetting();
    // Menu style box: background, border, title and the line under it, and the footer text
    void DrawPanel(int left, int top, int right, int bottom, LPCWSTR title, int first_item_y);
    void DrawPanelFooter(int left, int right, int bottom, LPCWSTR text, Color color = COLOR_GREY);
    int MenuItemBarHeight() const { return m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->menu_item_padding); }
    int MenuItemHeight() const { return MenuItemBarHeight() + m_Layout->menu_item_gap; }
    void DisplayStartMenu();
    // Submenus: false if the user went back (B), true if done (an action or Start)
    bool DisplayPartitionTableMenu();
    bool DisplayNewTableMenu();
    bool DisplayLoadTableMenu();
    bool GetLoadableTables(bool loadable[LOADED_TABLE_COUNT]);
    void LoadTable(LoadedTable which);
    void DisplayEditPartition();
    void LeaveEditPartition(int part);
    void DrawEditPartition(int part, int field, const bool field_available[], LPCWSTR step_name, LPCWSTR message, Color message_color, FLOAT bar_y, bool redraw_backdrop);
    void TypeEditValue(int part, int field, LPCWSTR &message, Color &message_color);
    bool RunNumberInput(LPCWSTR title, ULONGLONG current, ULONGLONG min, ULONGLONG max, int unit, ULONGLONG *result);
    void DrawNumberInput(LPCWSTR title, const wchar_t *digits, int unit, ULONGLONG current, ULONGLONG min, ULONGLONG max,
                         int selected_key, LPCWSTR message);
    int EditPanelTop() const;
    void DoEraseTable();
    void DoWriteTable();
    void DoFormatAll();
    void DoFormatOne();
    bool format_partition(const int i);

    // Disk and kernel partition table access
    ULONGLONG get_hdd_size();
    void read_drive_identify();
    bool read_lba48_info(XboxPartitionTable *p_table);
    bool reload_lba48();
    unsigned int get_cluster_size_from_partition(ANSI_STRING part);
    void RefreshClusterSizes();
    bool CheckIfPartitionTableChanged();
    int GetChangedPartitions(bool changed[XboxPartitionTableEntryCount], bool existing_only);
    void FormatPartitionList(const bool marked[XboxPartitionTableEntryCount], wchar_t *out, size_t out_count);
    LARGE_INTEGER get_backup_partition_offset_bytes();
    bool write_partition_tables(const XboxPartitionTable table, bool *backup_written = NULL);
    bool erase_partition_tables();
    XboxPartitionTable read_hdd_partition_table(LARGE_INTEGER offset);
    XboxPartitionTable read_hdd_main_partition_table();
    XboxPartitionTable read_hdd_backup_partition_table();

    Renderer m_Ui;
    const Layout *m_Layout; // For the video mode m_Ui runs in

    int TableCenterX() const { return (m_Layout->table_left + m_Layout->table_right) / 2; }
    // Top of a row ("slot") in the partition table panel: 0 is the column
    // headers, then the line under them, then the rest one pitch apart
    int TableSlotTop(int slot) const
    {
        return m_Layout->table_first_row_y + slot * m_Layout->table_row_height + (slot > 0 ? m_Layout->table_header_gap : 0);
    }
    // Slots of the section labels, and of partition i's row
    static int StandardLabelSlot() { return 1; }
    static int ExtendedLabelSlot() { return XBOX_STANDARD_PARTITION_COUNT + 2; }
    int TableRowTop(int i) const
    {
        return TableSlotTop(i + (i >= XBOX_STANDARD_PARTITION_COUNT ? 3 : 2));
    }

    ANSI_STRING m_PartitionPaths[XboxPartitionTableEntryCount];

    // Table being edited, and the one it was loaded from (to see what changed)
    XboxPartitionTableManipulator *m_pTable;
    XboxPartitionTableManipulator *m_pTableOriginal;
    XboxPartitionTable m_TableKernel;
    XboxPartitionTable m_TableHdd;
    XboxPartitionTable m_TableHddBackup;
    // The kernel's in-memory partition table, NULL without an lba48 patch
    XboxPartitionTable *m_pKernelTable;

    // Cluster size (KB) read from each partition's FATX header, 0 if unformatted/unreadable.
    // Cached because reading it means opening and reading every partition.
    unsigned int m_ClusterSizes[XboxPartitionTableEntryCount];

    // Controllers and the last input read by XInputGetStateAny
    HANDLE m_Controllers[4];
    int m_AnalogButton;
    int m_DigitalButtons;

    // Disk and kernel patch info
    bool m_FullLba48;
    ULONGLONG m_TotalHddSectors;
    bool m_Physical4k; // 512e drive: 4K physical sectors, decides where the backup table goes
    DriveInfo m_Drive;
    BackupStatus m_BackupStatus;
    DWORD m_Lba48Version;

    // Editor state
    int m_CurrentPart;
    // Main table selection bar, sliding towards m_CurrentPart's row
    FLOAT m_SelectionBarY;
    bool m_SelectionBarPlaced; // false until the first frame: start on the row, no slide
    DWORD m_SelectionBarTick;  // For SlideTowards()
    DpadRepeat m_MainScreenRepeat;
    int m_LoadedTable;  // LoadedTable
};

#endif // __XBPARTITIONER_H__

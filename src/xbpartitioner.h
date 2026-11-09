#ifndef __XBPARTITIONER_H__
#define __XBPARTITIONER_H__

#include <Xtl.h>
#include "partition.hpp"
#include "Undocumented.h"
#include "ui.h"
#include "layout.h"

// Shown in the title bar and on the About screen
#define XBPARTITIONER_VERSION L"2.0"

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

// Where the edited table came from
enum LoadedTable
{
    LOADED_TABLE_KERNEL = 0,
    LOADED_TABLE_HDD = 1,
    LOADED_TABLE_HDD_BACKUP = 2,
    LOADED_TABLE_COUNT, // Number of tables that can be loaded
    LOADED_TABLE_PRESET = LOADED_TABLE_COUNT
};

// From ATA IDENTIFY DEVICE
struct DriveInfo
{
    bool identify_ok;              // false: IDENTIFY failed, the rest is empty
    wchar_t model[41];
    wchar_t firmware[9];
    ULONGLONG native_sectors;      // Independent of the BIOS
    unsigned int physical_sector_size;
    bool physical_4k;              // 512e drive
};

enum BackupStatus
{
    BACKUP_NOT_USED = 0,  // No backup location: no patch, unknown size, or stock 8GB drive
    BACKUP_MISSING,       // Location known but no valid table there
    BACKUP_FOUND,
    BACKUP_FOUND_LEGACY,  // At the old location (last sector) on a 512e drive
    BACKUP_OVERLAPPED,    // The last partition covers the backup sector
    BACKUP_OVERLAPPED_MATCH // Covered, but the table there matches the main one
};

// RunMenu() results besides the selected item
#define MENU_RESULT_BACK -1  // B: back one level
#define MENU_RESULT_CLOSE -2 // Start: close all menus


// Held D-pad repeat, timed so screens keep drawing. Step() runs once per
// frame with the direction held and returns the move: at once, then after
// a delay, repeating.
struct DpadRepeat
{
    int held;
    DWORD last_move;
    bool repeating;

    DpadRepeat() : held(0), last_move(0), repeating(false) {}
    int Step(int direction);
};

// Eases a selection bar towards its target; time based (last_tick: 0 at first)
void SlideTowards(FLOAT &position, FLOAT target, DWORD &last_tick);

struct InfoRow
{
    LPCWSTR label;
    wchar_t value[80];
    Color color;
};

// What the editor shows, so the number pad can draw it behind itself
struct EditView
{
    int part;
    int field;
    const bool *field_available;
    LPCWSTR step_name;
    LPCWSTR message;
    Color message_color;
    FLOAT bar_y;
};

class App
{
public:
    App();
    ~App();

    void MainLoop();

private:
    // Layout space coordinates (see ui.h). The main screen is drawn behind
    // every other screen, every frame.
    void DrawMainScreen();
    void DrawTable();
    void DrawDiskMap();
    void FormatTableHeader(wchar_t *row);
    void DrawFreeSpace();
    void DrawLoadedTableName();
    void DrawControlsHint();
    bool RunDialog(LPCWSTR title, Color title_color, const LPCWSTR *lines, int line_count, LPCWSTR ok_label, LPCWSTR cancel_label);
    void AnimatePanelOpen(int left, int top, int right, int bottom);
    void AnimatePanelClose(int left, int top, int right, int bottom);
    void AnimatePanel(int left, int top, int right, int bottom, bool opening);
    bool ConfirmWarning(const LPCWSTR *lines, int line_count, LPCWSTR ok_label);
    void ShowWarning(const LPCWSTR *lines, int line_count);
    void ShowError(const LPCWSTR* lines, int line_count);
    void FadeIn();
    void FadeOut();

    void ShowStatus(LPCWSTR message, Color color);
    void ShowMessage(LPCWSTR title, Color title_color, LPCWSTR message);
    // A one line window ("Saving settings...") around slow work: open it, do
    // the work, close it with what OpenBusyWindow returned
    DWORD OpenBusyWindow(LPCWSTR message);
    void CloseBusyWindow(LPCWSTR message, DWORD opened);
    void DrawBusyWindow(LPCWSTR message);
    void BusyWindowBox(int *top, int *bottom);
    void DisplaySystemInfo();
    bool SystemInfoNeedsAttention();
    void DisplayAbout();
    void ShowInfoPanel(LPCWSTR title, const InfoRow *rows, int row_count);
    void OfferBackupRestore();
    bool IsMainTableUsable();
    bool IsBackupTableUsable();
    void CheckForV4TableOnOldBios();
    void WarnUnalignedPartitions();
    bool ConfirmFormatWarning(bool format_changed);
    bool DisplayNoInfo();
    void CheckTableStatus();
    void RunStartupChecks();
    bool ConfirmExit();
    void QuitToDashboard();

    void FormatPartitionRow(const int partition_number, wchar_t *row);

    int XInputGetStateAny(int *analog, int *buttons, int wait = 2);
    void UpdateControllers();
    bool CheckStellar();
    void WarnStellarDetected();
    // False (with an empty state) if the pad is gone or the read failed
    bool ReadController(int port, XINPUT_STATE *state);
    // False while m_WaitForRelease is set, until everything is released, so a
    // held button counts once and never carries over to the next screen
    bool ReadInput();
    void HandleMainScreenButtons(int buttons, int analog);
    void UpdateSelectionBar();
    void ApplyPreset(int preset);
    void UndoAllChanges();

    void WarnBackupOverlapped();
    // Covered but matching: shrink the last partition by 4 KB to clear it.
    // False if that isn't possible (nothing was asked).
    bool OfferShrinkForBackup();
    // Its FATX layout stays and the 4 KB it loses holds no used cluster
    bool ShrinkKeepsData(int partition);
    // Called with the selected item at the start and whenever it changes
    typedef void (App::*MenuPreview)(int item);
    int RunMenu(LPCWSTR title, const LPCWSTR *labels, const bool *enabled, int count, int initial = 0, bool submenu = false,
                MenuPreview preview = NULL);
    const Theme &Colors() const { return m_Ui.GetTheme(); }
    bool DisplayThemeMenu();
    void PreviewTheme(int index) { m_Ui.SetTheme(index); }
    void LoadSettings();
    void SaveSettings();
    // "Partition N (X) holds this program: <what>"
    void FormatLaunchPartitionLine(wchar_t *out, LPCWSTR what);
    // True if table moves, resizes or removes the partition this program runs from
    bool MovesLaunchPartition(const XboxPartitionTable &table) const;
    void DrawPanel(int left, int top, int right, int bottom, LPCWSTR title, int first_item_y);
    void DrawPanelFooter(int left, int right, int bottom, LPCWSTR text, Color color = COLOR_GREY);
    int MenuItemBarHeight() const { return m_Ui.TextBoxHeight(FONT_TEXT, m_Layout->menu_item_padding); }
    int MenuItemHeight() const { return MenuItemBarHeight() + m_Layout->menu_item_gap; }
    void DisplayStartMenu();
    // Submenus: false if the user went back with B
    bool DisplayPartitionTableMenu();
    bool DisplayNewTableMenu();
    bool DisplayLoadTableMenu();
    bool GetLoadableTables(bool loadable[LOADED_TABLE_COUNT]);
    void LoadTable(LoadedTable which);
    void DisplayEditPartition();
    void LeaveEditPartition(int part);
    // Just the panel, inside BeginFrame()/EndFrame()
    void DrawEditPanel(const EditView &view);
    // While the number pad is open: the editor behind it
    const EditView *m_pActiveEditView;
    void TypeEditValue(int part, int field, LPCWSTR &message, Color &message_color);
    bool RunNumberInput(LPCWSTR title, ULONGLONG current, ULONGLONG min, ULONGLONG max, int unit, ULONGLONG *result);
    void DrawNumberInput(LPCWSTR title, const wchar_t *digits, int unit, ULONGLONG current, ULONGLONG min, ULONGLONG max,
                         int selected_key, LPCWSTR message);
    int EditPanelTop() const;
    int EditPanelHeight() const;
    int NumberInputHeight();
    void DoEraseTable();
    // Explains why a table can't be written and returns true: a partition in
    // the reserved area, or overlapping or shared starts
    bool RefuseUnwritableTable(const XboxPartitionTable &table);
    // what: the end of the advice, e.g. "before writing."
    void ShowReservedAreaWarning(int partition, LPCWSTR what);
    void CheckTableVersions();
    // Only a backup table on disk: a Titan BIOS, or restore it as the main one
    void OfferTitanOrRestore();
    void DoWriteTable();
    void DoFormatAll();
    void DoFormatOne();
    bool format_partition(const int i);

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
    // With a Titan BIOS, asks whether to write sector 0 too (*main_written)
    bool write_partition_tables(const XboxPartitionTable table, bool *backup_written = NULL, bool *main_written = NULL);
    bool erase_partition_tables();
    XboxPartitionTable read_hdd_partition_table(LARGE_INTEGER offset);
    bool read_hdd_sector(ULONGLONG lba, BYTE *sector);
    XboxPartitionTable read_hdd_main_partition_table();
    XboxPartitionTable read_hdd_backup_partition_table();

    Renderer m_Ui;
    const Layout *m_Layout; // For the video mode m_Ui runs in

    int TableCenterX() const { return (m_Layout->table_left + m_Layout->table_right) / 2; }
    // Slot 0 is the column headers, then the line under them, then one pitch apart
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

    // FATX cluster size (KB) per partition, 0 if none. Cached: reading it means
    // opening every partition.
    unsigned int m_ClusterSizes[XboxPartitionTableEntryCount];

    HANDLE m_Controllers[4];
    int m_AnalogButton;
    int m_DigitalButtons;
    bool m_WaitForRelease; // See ReadInput()

    // Partition this program (and D:) runs from, -1 if a DVD or unknown
    int m_LaunchPartition;
    // Set once D:'s partition changes: writing through D: would hit the wrong
    // sectors, so the settings aren't saved anymore
    bool m_ThemeSaveBlocked;
    // animations= in the settings
    bool m_Animations;
    // force_lba48_v3= in the settings, see read_lba48_info()
    bool m_ForceLba48V3;
    // Titan patched BIOS (it only uses the backup table): the user said so
    // (OfferTitanOrRestore), or the backup has Titan's version
    bool m_TitanBios;

    bool m_FullLba48;
    ULONGLONG m_TotalHddSectors;
    bool m_Physical4k; // 512e: decides where the backup table goes
    DriveInfo m_Drive;
    BackupStatus m_BackupStatus;
    DWORD m_Lba48Version;

    int m_CurrentPart;
    FLOAT m_SelectionBarY;
    bool m_SelectionBarPlaced; // false until the first frame: start on the row, no slide
    DWORD m_SelectionBarTick;  // For SlideTowards()
    DpadRepeat m_MainScreenRepeat;
    int m_LoadedTable;  // LoadedTable
};

#endif // __XBPARTITIONER_H__

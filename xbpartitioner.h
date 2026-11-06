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

// What the Start button does, cycled with White
enum Function
{
    FUNCTION_WRITE_TABLE = 0,
    FUNCTION_WRITE_TABLE_FORMAT = 1,
    FUNCTION_FORMAT_CURRENT_PARTITION = 2,
    FUNCTION_COUNT
};

// Which partition table is loaded in the editor, cycled with Y
enum LoadedTable
{
    LOADED_TABLE_KERNEL = 0,
    LOADED_TABLE_HDD = 1,
    LOADED_TABLE_HDD_BACKUP = 2,
    LOADED_TABLE_COUNT
};

// Index into XINPUT_GAMEPAD::bAnalogButtons as reported by XInputGetStateAny
#define ANALOG_BUTTON_Y 3

class App
{
public:
    App();
    ~App();

    void MainLoop();

private:
    // Screens. All coordinates are in layout space, see ui.h.
    void DrawMainScreen();
    void DrawTable();
    void DrawFreeSpace();
    void DrawFormatCluster();
    void DrawLoadedTableName();
    void DrawStartActionHint();
    void DrawYPrompt(LPCWSTR action, int x, int y);
    void DrawWarningScreen(const LPCWSTR *lines, int line_count, LPCWSTR y_prompt_action);
    void FadeIn();

    void ShowStatus(LPCWSTR message, Color color, bool wait_for_button);
    bool ConfirmOnMainScreen(LPCWSTR message);
    void DisplayHelp();
    void DisplayWarningBackupAvailable();
    bool ConfirmFormatWarning();
    bool DisplayNoInfo();
    void CheckTableStatus();
    void QuitToDashboard();

    bool PreparePartitionInfoStrings(const int partition_number, wchar_t *partno_str, wchar_t *size_string, wchar_t *cluster_string, wchar_t *lba_start_str, wchar_t *lba_size_str);

    // Input
    int XInputGetStateAny(int *analog, int *analog_val, int *buttons, short *l_thumb_y_val, int wait = 2);
    void WaitForButton();
    bool ConfirmWithY();
    // Returns true if the button press should be waited out before the next poll
    bool HandleAnalogButton(int button, int value);
    bool HandleDigitalButtons(int buttons, short left_thumb_y);
    void CycleLayoutScheme();
    void CycleLoadedTable();
    void ChangeSelectedPartition(int trigger_value, int direction, bool fine);

    // Actions bound to Start
    void DoStart();
    void DoWriteTable();
    void DoFormatAll();
    void DoFormatOne();
    bool format_partition(const int i);

    // Disk and kernel partition table access
    ULONGLONG get_hdd_size();
    bool read_lba48_info(XboxPartitionTable *p_table);
    bool reload_lba48();
    unsigned int get_cluster_size_from_partition(ANSI_STRING part);
    void RefreshClusterSizes();
    bool CheckIfPartitionTableChanged();
    LARGE_INTEGER get_backup_partition_offset_bytes();
    bool write_partition_tables(const XboxPartitionTable table);
    bool erase_partition_tables();
    XboxPartitionTable read_hdd_partition_table(LARGE_INTEGER offset);
    XboxPartitionTable read_hdd_main_partition_table();
    XboxPartitionTable read_hdd_backup_partition_table();

    Renderer m_Ui;

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
    int m_AnalogValue;
    int m_DigitalButtons;
    short m_LeftThumbY;

    // Disk and kernel patch info
    bool m_FullLba48;
    ULONGLONG m_TotalHddSectors;
    DWORD m_Lba48Version;

    // Editor state
    int m_CurrentPart;
    bool m_EditStart;   // true: size controls move the start, false: they resize
    int m_StartAction;  // Function
    int m_LayoutScheme; // Next standard layout for the A button
    int m_LoadedTable;  // LoadedTable
};

#endif // __XBPARTITIONER_H__

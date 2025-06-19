#ifndef __XBPARTITIONER_H__
#define __XBPARTITIONER_H__

#include <xfont.h> //used for directX textout
#include "partition.hpp"
#include <Xtl.h>
#include "Undocumented.h"

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

struct SCREENVERTEX
{
    D3DXVECTOR4 pos; // The transformed position for the vertex point.
    DWORD color;     // The vertex color.
};

#define D3DFVF_SCREENVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

enum Function
{
    FUNCTION_WRITE_TABLE = 0,
    FUNCTION_WRITE_TABLE_FORMAT = 1,
    FUNCTION_FORMAT_CURRENT_PARTITION = 2
};

enum Color
{
    COLOR_WHITE = 0,
    COLOR_RED = 1,
    COLOR_GREEN = 2,
    COLOR_BLUE = 3,
    COLOR_YELLOW = 4,
    COLOR_WHITE_BG = 5,
    COLOR_RED_BG = 6,
    COLOR_GREY = 7,
    COLOR_DARK_YELLOW = 8
};

class App {
    public:
        App();
        ~App();

        void MainLoop();

        void InitialiseD3D();
        void CleanUpD3D();

        int XInputGetStateAny(int *analog, int *analog_val, int *buttons, short *l_thumb_y_val, int wait = 2);
        void DrawRect(FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, DWORD dwStartColor, DWORD dwEndColor);
        void DrawText(XFONT *font, LPCWSTR text_string, ULONG xpos, ULONG ypos, Color color, int just = XFONT_LEFT);

        void DisplayText(LPCWSTR text_string, ULONG xpos, ULONG ypos, Color color, int just = XFONT_LEFT);
        void DisplayTableText(LPCWSTR text_string, ULONG xpos, ULONG ypos, Color color, int just = XFONT_LEFT);
        void DisplayWarningBackupAvailable();
        void DisplayWarning();
        bool DisplayNoInfo();

        bool format_partition(const int i);
        ULONGLONG get_hdd_size();
        bool read_lba48_info(XboxPartitionTable *p_table);
        bool reload_lba48();
        unsigned int get_cluster_size_from_partition(ANSI_STRING part);
        bool CheckIfPartitionTableChanged();
        LARGE_INTEGER get_backup_partition_offset_bytes();
        bool write_partition_tables(const XboxPartitionTable table);
        bool erase_partition_tables();
        XboxPartitionTable read_hdd_partition_table(LARGE_INTEGER offset);

        XboxPartitionTable read_hdd_main_partition_table();
        XboxPartitionTable read_hdd_backup_partition_table();
        void DisplayHelp();

        bool PreparePartitionInfoStrings(const int partition_number, wchar_t * partno_str, wchar_t * size_string, wchar_t * cluster_string, wchar_t * lba_start_str, wchar_t * lba_end_str);
        void DrawTable();
        void DrawAvailableTables();
        void DrawInfo();
        void DrawScreen(int g_iCurrentPart);
        void DrawFreeSpace();
        void DrawFormatCluster();
        void CheckTableStatus();
        void DrawLastLoadedTable(const int table_id);
        void DoFormatAll();
        void DoWriteTable();
        void DoFormatOne();
        void DoStart();
        void part_change_size(int TrigValue, int change_start, int Direction, bool fine);

    private:
        ANSI_STRING partition_str[XboxPartitionTableEntryCount];

        XboxPartitionTableManipulator *tableManipulator;
        XboxPartitionTableManipulator *tableManipulatorOriginal;
        XboxPartitionTable tableKernel;
        XboxPartitionTable tableHDD;
        XboxPartitionTable tableHDDBackup;
        XboxPartitionTable *KernelPartTblAdd;

        // D3D Vars
        unsigned int SCREEN_SIZE_X;
        unsigned int SCREEN_SIZE_Y;

        LPDIRECT3D8 g_pD3D;
        LPDIRECT3DDEVICE8 g_pD3DDevice;
        LPDIRECT3DTEXTURE8 g_pBackTexture;

        XFONT *g_pLucon18BitmapFont;
        XFONT *g_pArial18BitmapFont;
        XFONT *g_pArial32BitmapFont;

        // Controller handler
        HANDLE g_Controller[4];
        int aButton, aButtonVal, tButton;
        short aLThumbY;

        // Global vars for part table
        bool g_full_lba48;
        ULONGLONG g_TotalHDDSectors;
        DWORD g_lba48_version;

        int g_iCurrentPart;
        int g_start_size_selector;
        int g_iFunction;
};

#endif // __XBPARTITIONER_H__

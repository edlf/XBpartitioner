// Startup, shutdown and the main loop. The rest of App is in app_*.cpp.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// Path of the running xbe, e.g. \Device\Harddisk0\Partition6\Apps\XBP\default.xbe
extern "C" __declspec(dllimport) ANSI_STRING XeImageFileName;

// Table index of the partition the xbe runs from, -1 if not on the hard drive
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

App::App()
    : m_Layout(&g_Layout480), m_pTable(NULL), m_pTableOriginal(NULL), m_pKernelTable(NULL), m_AnalogButton(-1), m_DigitalButtons(0), m_FullLba48(false),
      m_TotalHddSectors(0), m_Physical4k(false), m_BackupStatus(BACKUP_NOT_USED), m_Lba48Version(0), m_CurrentPart(0), m_SelectionBarY(0.0f),
      m_SelectionBarPlaced(false), m_SelectionBarTick(0), m_LoadedTable(LOADED_TABLE_KERNEL), m_pActiveEditView(NULL), m_WaitForRelease(false),
      m_LaunchPartition(-1), m_ThemeSaveBlocked(false), m_Animations(true), m_ForceLba48V3(false), m_ResizeKeepsData(false), m_TitanBios(false)
{
    memset(m_ClusterSizes, 0, sizeof(m_ClusterSizes));
    memset(m_Usage, 0, sizeof(m_Usage));
    memset(m_UsageKnown, 0, sizeof(m_UsageKnown));
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
    LoadSettings();

    XDEVICE_PREALLOC_TYPE deviceTypes[] = {
        {XDEVICE_TYPE_GAMEPAD, 4},
    };
    XInitDevices(ARRAY_COUNT(deviceTypes), deviceTypes);

    // Give the controllers time to be enumerated
    Sleep(2000);

    // Later ones are picked up by UpdateControllers()
    const DWORD present = XGetDevices(XDEVICE_TYPE_GAMEPAD);
    for (int i = 0; i < ARRAY_COUNT(m_Controllers); i++)
    {
        if (present & (1 << i))
        {
            m_Controllers[i] = XInputOpen(XDEVICE_TYPE_GAMEPAD, i, XDEVICE_NO_SLOT, 0);
        }
    }

    m_TotalHddSectors = get_hdd_size();
    // Before any table manipulator: it decides where the backup table goes
    read_drive_identify();
    m_Physical4k = m_Drive.physical_4k;

    // Also creates m_pTableOriginal; without a patch m_pKernelTable stays NULL
    reload_lba48();

    m_TableHdd = read_hdd_main_partition_table();
    m_TableHddBackup = read_hdd_backup_partition_table();

    RefreshClusterSizes();

    m_CurrentPart = m_pTable->GetFirstAvailablePartitionSlot();

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
    RunStartupChecks();

    // One frame per pass: Present waits for the vertical blank
    while (true)
    {
        UpdateSelectionBar();

        m_Ui.BeginFrame();
        DrawMainScreen();
        DrawControlsHint();
        m_Ui.EndFrame();

        if (ReadInput())
        {
            HandleMainScreenButtons(m_DigitalButtons, m_AnalogButton);
        }
    }
}

// Only selects and opens the editor or menu, so a stray press can't write anything
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
        DisplayEditPartition();
    }
    else if (buttons & XINPUT_GAMEPAD_START)
    {
        DisplayStartMenu();
    }
}

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

// Back to the last written (or startup) table
void App::UndoAllChanges()
{
    LoadTable(LOADED_TABLE_KERNEL);
}

// True for Exit; warns first about unwritten changes
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

bool App::CheckStellar()
{
    const char *stellarSignature = "stellar";
    size_t stellarSignatureLen = strlen(stellarSignature);
    BYTE *stellarScanStart = (BYTE *)0x80010000;
    BYTE *stellarScanEnd = (BYTE *)0x80026800; // 90kb

    for (BYTE *scanPtr = stellarScanStart; scanPtr < stellarScanEnd - stellarSignatureLen; scanPtr++)
    {
        if (_memicmp(scanPtr, stellarSignature, stellarSignatureLen) == 0)
        {
            return true;
        }
    }

    return false;
}

// Does not return
void App::QuitToDashboard()
{
    FadeOut();
    m_Ui.Shutdown();

    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};
    XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
}

void __cdecl main()
{
    App app;
    app.MainLoop();
}

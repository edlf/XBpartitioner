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
    : m_layout(&g_Layout480),
     
      m_launch_partition(launch_partition_index()),
      m_filesystems(*this), m_tables(m_hdd), m_dialogs(m_ui, m_input, *this), m_menus(m_ui, m_input, m_dialogs),
      m_main_screen(m_ui, MainScreenSources{m_session, m_tables, m_filesystems}),
      m_editor(ScreenTools{m_ui, m_input, m_dialogs}, EditorSources{m_session, m_filesystems, m_settings}),
      m_fsck(m_dialogs, m_session, m_launch_partition),
      m_writer(TableWriterParts{m_tables, m_session, m_filesystems, m_fsck, m_dialogs, m_settings, m_launch_partition}),
      m_info(m_dialogs, InfoSources{m_tables, m_session, m_settings, m_drive, m_ui}),
      m_checks(StartupCheckParts{m_tables, m_session, m_hdd, m_dialogs, m_writer, m_fsck, m_info, m_settings, m_launch_partition})
{
    memset(&m_drive, 0, sizeof(m_drive));

    m_ui.Init();
    m_layout = (m_ui.GetVideoMode() == VIDEO_MODE_720P) ? &g_Layout720 : &g_Layout480;
    LoadSettings();
    m_dialogs.Configure(m_layout, m_settings.Animations());
    m_menus.Configure(m_layout);
    m_main_screen.Configure(m_layout);
    m_editor.Configure(m_layout);

    XDEVICE_PREALLOC_TYPE deviceTypes[] = {
        {XDEVICE_TYPE_GAMEPAD, 4},
    };
    XInitDevices(ArrayLength(deviceTypes), deviceTypes);

    // Give the controllers time to be enumerated
    Sleep(2000);

    m_input.OpenPresentPads();

    // Before any table editor: the drive decides where the backup table goes
    m_drive = m_hdd.ReadIdentify();
    m_tables.ReadDiskSize(m_drive.physical_4k);

    // Also starts the editor, from the kernel's table if it has the patch
    m_writer.StartFromKernelTable();

    m_tables.ReadDiskTables();

    m_filesystems.RefreshAll();

    m_main_screen.Select(m_session.Edited().GetFirstAvailablePartitionSlot());

    FadeIn();
}

App::~App()
{
    m_ui.Shutdown();
}

void App::MainLoop()
{
    // QuitToDashboard() doesn't return
    if (!m_checks.Run())
    {
        QuitToDashboard();
    }

    // One frame per pass: Present waits for the vertical blank
    while (true)
    {
        m_main_screen.UpdateSelectionBar();

        m_ui.BeginFrame();
        m_main_screen.Draw();
        m_main_screen.DrawControlsHint();
        m_ui.EndFrame();

        if (m_input.Read())
        {
            HandleMainScreenButtons(m_input.DigitalButtons(), m_input.AnalogButton());
        }
    }
}

// Only selects and opens the editor or menu, so a stray press can't write anything
void App::HandleMainScreenButtons(int buttons, int analog)
{
    const int direction = (buttons & XINPUT_GAMEPAD_DPAD_UP) ? -1 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;

    m_main_screen.MoveSelection(direction);
    if (direction != 0)
    {
        return;
    }

    if (analog == XINPUT_GAMEPAD_A)
    {
        m_editor.Run(m_main_screen.SelectedPartition());
    }
    else if (buttons & XINPUT_GAMEPAD_START)
    {
        DisplayStartMenu();
    }
}



// True for Exit; warns first about unwritten changes
bool App::ConfirmExit()
{
    if (m_session.Changes().Any())
    {
        static const LPCWSTR lines[] = {
            L"The partition table has changes that were not written.",
            L"They will be lost when you exit.",
        };
        return m_dialogs.ConfirmWarning(lines, ArrayLength(lines), L"Exit");
    }

    static const LPCWSTR lines[] = {
        L"Exit XBpartitioner and return to the dashboard?",
    };
    return m_dialogs.RunDialog(DialogText{L"Exit", COLOR_WHITE, lines, ArrayLength(lines)}, DialogButtons{L"Exit", L"Cancel"});
}

// Does not return
void App::QuitToDashboard()
{
    FadeOut();
    m_ui.Shutdown();

    LD_LAUNCH_DASHBOARD LaunchDashData = {XLD_LAUNCH_DASHBOARD_MAIN_MENU};
    XLaunchNewImage(NULL, (LAUNCH_DATA *)&LaunchDashData);
}

void __cdecl main()
{
    App app;
    app.MainLoop();
}

#ifndef __XBPARTITIONER_H__
#define __XBPARTITIONER_H__

#include <Xtl.h>
#include "partition.hpp"
#include "fatx_check.hpp"
#include "fatx_resize.hpp"
#include "partition_usage.h"
#include "partition_set.h"
#include "partition_table_changes.h"
#include "filesystem_cache.h"
#include "settings.h"
#include "gamepad_input.h"
#include "hdd_access.h"
#include "partition_table_store.h"
#include "partition_table_session.h"
#include "dialog_presenter.h"
#include "menu_runner.h"
#include "main_screen.h"
#include "partition_editor_screen.h"
#include "fsck_workflow.h"
#include "info_screens.h"
#include "table_writer.h"
#include "startup_checks.h"
#include "disk_partition_device.h"
#include "selection_slide.h"
#include "Undocumented.h"
#include "ui.h"
#include "layout.h"

// Reads file systems for its FilesystemCache, and draws what is behind its panels
class App : private FilesystemReader, private ScreenBackdrop
{
  public:
    App();
    ~App();

    void MainLoop();

  private:
    // Layout space coordinates (see ui.h)
    // ScreenBackdrop
    void DrawBackdrop();
    void FadeIn();
    void FadeOut();

    bool ConfirmExit();
    void QuitToDashboard();


    void HandleMainScreenButtons(int buttons, int analog);

    bool DisplayThemeMenu();
    void LoadSettings();
    void SaveSettings();
    void DisplayStartMenu();
    // Does what the Start menu's choice says; false to show the menu again
    bool HandleStartMenuChoice(int choice);
    // Submenus: false if the user went back with B
    bool DisplayPartitionTableMenu();
    bool DisplayNewTableMenu();
    bool DisplayLoadTableMenu();
    bool GetLoadableTables(bool loadable[LOADED_TABLE_COUNT]);
    void LoadTable(LoadedTable which);
    // FilesystemReader
    unsigned int ReadClusterSizeKb(int partition);
    bool ReadUsage(int partition, FatxUsage *usage);


    Renderer m_ui;
    const Layout *m_layout; // For the video mode m_ui runs in


    // The table being edited, and the one in use it started from
    PartitionTableSession m_session;

    FilesystemCache m_filesystems;

    GamepadInput m_input;
    HddAccess m_hdd;
    PartitionTableStore m_tables;

    // Partition this program (and D:) runs from, -1 if a DVD or unknown
    int m_launch_partition;
    // xbpartitioner.ini. Resizing keeps data off: resized partitions are
    // always formatted and their usage isn't read.
    Settings m_settings;
    DialogPresenter m_dialogs;
    MenuRunner m_menus;
    MainScreen m_main_screen;
    PartitionEditorScreen m_editor;
    FsckWorkflow m_fsck;
    TableWriter m_writer;

    DriveInfo m_drive;
    InfoScreens m_info;
    StartupChecks m_checks;

};

#endif // __XBPARTITIONER_H__

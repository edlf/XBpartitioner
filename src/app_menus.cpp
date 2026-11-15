// The Start menu and its submenus.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"
#include "array_length.h"
#include "partition_format.h"

// Shows each theme while it is selected in the Theme menu
class ThemePreview : public MenuPreview
{
  public:
    explicit ThemePreview(Renderer &ui) : m_ui(ui)
    {
    }

    void Preview(int item)
    {
        m_ui.SetTheme(item);
    }

  private:
    Renderer &m_ui;
};

// Order of the Start menu, must match the labels in StartMenuItems
enum MenuItem
{
    MENU_EDIT_PARTITION = 0,
    MENU_FORMAT_SELECTED,
    MENU_CHECK_SELECTED,
    MENU_UNDO_ALL,
    MENU_PARTITION_TABLE,
    MENU_SYSTEM_INFO,
    MENU_RESIZE_KEEPS_DATA,
    MENU_THEME,
    MENU_ABOUT,
    MENU_EXIT,
    MENU_ITEM_COUNT
};

// The Start menu's labels and enabled items, for the selected partition
class StartMenuItems
{
  public:
    StartMenuItems(int partition, const PartitionTableSession &session, const Settings &settings)
    {
        FillLabels(partition, session, settings);
        FillEnabled(partition, session);
    }

    MenuRequest Request(int selected) const
    {
        const MenuRequest menu = {L"Menu", m_labels, m_enabled, MENU_ITEM_COUNT, selected, MENU_FOOTER_TOP_LEVEL, NULL};
        return menu;
    }

  private:
    void FillLabels(int partition, const PartitionTableSession &session, const Settings &settings)
    {
        FormatWide(m_edit_label, ArrayLength(m_edit_label), L"Edit partition %d (%c)", partition + 1, PartitionLetter(partition));
        FormatWide(m_format_label, ArrayLength(m_format_label), L"Format partition %d (%d KB clusters)", partition + 1, session.Edited().CalculateClusterSizeForPartition(partition));
        FormatWide(m_check_label, ArrayLength(m_check_label), L"Check file system on partition %d", partition + 1);

        m_labels[MENU_EDIT_PARTITION] = m_edit_label;
        m_labels[MENU_FORMAT_SELECTED] = m_format_label;
        m_labels[MENU_CHECK_SELECTED] = m_check_label;
        m_labels[MENU_UNDO_ALL] = L"Undo all changes since the last write";
        m_labels[MENU_PARTITION_TABLE] = L"Partition table...";
        m_labels[MENU_SYSTEM_INFO] = L"System information";
        m_labels[MENU_RESIZE_KEEPS_DATA] = settings.ResizeKeepsData() ? L"Resizing keeps data: On" : L"Resizing keeps data: Off";
        m_labels[MENU_THEME] = L"Theme...";
        m_labels[MENU_ABOUT] = L"About";
        m_labels[MENU_EXIT] = L"Exit to dashboard";
    }

    void FillEnabled(int partition, const PartitionTableSession &session)
    {
        for (int i = 0; i < MENU_ITEM_COUNT; i++)
        {
            m_enabled[i] = true;
        }
        m_enabled[MENU_FORMAT_SELECTED] = session.Edited().IsPartitionEnabled(partition);
        // Checks what is on disk, i.e. the written table
        m_enabled[MENU_CHECK_SELECTED] = session.InUse().IsPartitionEnabled(partition);
        m_enabled[MENU_UNDO_ALL] = session.Changes().Any();
    }

    wchar_t m_edit_label[60];
    wchar_t m_format_label[60];
    wchar_t m_check_label[60];
    LPCWSTR m_labels[MENU_ITEM_COUNT]; // Indexed by MenuItem
    bool m_enabled[MENU_ITEM_COUNT];
};

void App::DisplayStartMenu()
{
    // Coming back from a submenu: on the item they came from
    int selected = 0;
    do
    {
        const StartMenuItems items(m_main_screen.SelectedPartition(), m_session, m_settings);
        selected = m_menus.Run(items.Request(selected));
    } while (!HandleStartMenuChoice(selected));
}

bool App::HandleStartMenuChoice(int choice)
{
    switch (choice)
    {
    case MENU_EDIT_PARTITION:
        m_editor.Run(m_main_screen.SelectedPartition());
        return true;
    case MENU_FORMAT_SELECTED:
        m_writer.FormatOne(m_main_screen.SelectedPartition());
        return true;
    case MENU_CHECK_SELECTED:
        m_fsck.CheckFileSystem(m_main_screen.SelectedPartition());
        return true;
    case MENU_PARTITION_TABLE:
        return DisplayPartitionTableMenu();
    case MENU_UNDO_ALL:
        m_session.UndoAllChanges();
        return true;
    case MENU_SYSTEM_INFO:
        m_info.ShowSystemInfo();
        return true;
    case MENU_RESIZE_KEEPS_DATA:
        m_settings.ToggleResizeKeepsData();
        SaveSettings();
        return false; // Stays in the menu so the new state shows
    case MENU_THEME:
        return DisplayThemeMenu();
    case MENU_ABOUT:
        m_info.ShowAbout();
        return true;
    case MENU_EXIT:
        if (ConfirmExit())
        {
            QuitToDashboard();
        }
        return true;
    default:
        return true;
    }
}

// Moving through the list previews each theme; A keeps and saves it.
// False if the user went back with B.
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

    const int original = m_ui.GetThemeIndex();
    ThemePreview preview(m_ui);
    const MenuRequest menu = {L"Theme", labels, enabled, count, original, MENU_FOOTER_SUBMENU, &preview};
    const int chosen = m_menus.Run(menu);

    if (chosen < 0)
    {
        m_ui.SetTheme(original);
        return chosen == MENU_RESULT_CLOSE;
    }

    m_ui.SetTheme(chosen);
    if (chosen != original)
    {
        SaveSettings();
    }
    return true;
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

// Actions on the whole table. False if the user went back with B.
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

    int selected = 0;

    while (true)
    {
        bool enabled[TABLE_MENU_ITEM_COUNT];
        for (int i = 0; i < TABLE_MENU_ITEM_COUNT; i++)
        {
            enabled[i] = true;
        }
        bool loadable[LOADED_TABLE_COUNT];
        enabled[TABLE_MENU_LOAD_TABLE] = GetLoadableTables(loadable);

        const MenuRequest menu = {L"Partition table", labels, enabled, TABLE_MENU_ITEM_COUNT, selected, MENU_FOOTER_SUBMENU, NULL};
        selected = m_menus.Run(menu);

        switch (selected)
        {
        case MENU_RESULT_BACK:
            return false;
        case TABLE_MENU_WRITE:
            m_writer.WriteEditedTable();
            return true;
        case TABLE_MENU_WRITE_AND_FORMAT:
            m_writer.WriteAndFormatChanged();
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
            m_writer.EraseTables();
            return true;
        default:
            return true;
        }
    }
}

// Nothing is written. False if the user went back with B.
bool App::DisplayNewTableMenu()
{
    // Indexed by TablePreset
    static const LPCWSTR labels[PRESET_COUNT] = {
        L"Stock partitions only",
        L"Stock + F (rest of the disk)",
        L"Stock + F and G (split the rest)",
    };

    static const bool enabled[PRESET_COUNT] = {true, true, true};

    const MenuRequest menu = {L"New partition table", labels, enabled, PRESET_COUNT, 0, MENU_FOOTER_SUBMENU, NULL};
    const int choice = m_menus.Run(menu);
    if (choice == MENU_RESULT_BACK)
    {
        return false;
    }

    if (choice >= 0)
    {
        m_session.ApplyPreset((TablePreset)choice);
    }
    return true;
}

// Indexed by LoadedTable. Missing or broken tables can't be loaded (that
// would wipe the layout), the kernel's needs the patch. True if any can.
bool App::GetLoadableTables(bool loadable[LOADED_TABLE_COUNT])
{
    loadable[LOADED_TABLE_KERNEL] = (m_tables.HasLba48Patch());
    loadable[LOADED_TABLE_HDD] = m_tables.IsMainTableUsable();
    loadable[LOADED_TABLE_HDD_BACKUP] = m_tables.IsBackupTableUsable();

    return loadable[LOADED_TABLE_KERNEL] || loadable[LOADED_TABLE_HDD] || loadable[LOADED_TABLE_HDD_BACKUP];
}

// Nothing is written. False if the user went back with B.
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

    const MenuRequest menu = {L"Load partition table", labels, loadable, LOADED_TABLE_COUNT, 0, MENU_FOOTER_SUBMENU, NULL};
    const int choice = m_menus.Run(menu);
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


// Only the editor changes (no confirmation): write it to make it active
void App::LoadTable(LoadedTable which)
{
    switch (which)
    {
    case LOADED_TABLE_KERNEL:
        m_session.UndoAllChanges();
        break;
    case LOADED_TABLE_HDD:
        m_session.Load(m_tables.MainTable(), which);
        break;
    case LOADED_TABLE_HDD_BACKUP:
        m_session.Load(m_tables.BackupTable(), which);
        break;
    default:
        break;
    }
}

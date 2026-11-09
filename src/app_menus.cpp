// The Start menu and its submenus.

#include "stdafx.h"
#include "xbpartitioner.h"
#include "app_internal.h"

// Order of the Start menu, must match labels[] in DisplayStartMenu()
enum MenuItem
{
    MENU_EDIT_PARTITION = 0,
    MENU_FORMAT_SELECTED,
    MENU_CHECK_SELECTED,
    MENU_UNDO_ALL,
    MENU_PARTITION_TABLE,
    MENU_SYSTEM_INFO,
    MENU_THEME,
    MENU_ABOUT,
    MENU_EXIT,
    MENU_ITEM_COUNT
};

void App::DisplayStartMenu()
{
    // Coming back from a submenu: on the item they came from
    int selected = 0;

    while (true)
    {
        wchar_t edit_label[60];
        swprintf(edit_label, L"Edit partition %d (%c)", m_CurrentPart + 1, partition_letter(m_CurrentPart));

        wchar_t format_label[60];
        swprintf(format_label, L"Format partition %d (%d KB clusters)", m_CurrentPart + 1, m_pTable->CalculateClusterSizeForPartition(m_CurrentPart));

        wchar_t check_label[60];
        swprintf(check_label, L"Check file system on partition %d", m_CurrentPart + 1);

        // Indexed by MenuItem
        const LPCWSTR labels[MENU_ITEM_COUNT] = {
            edit_label,
            format_label,
            check_label,
            L"Undo all changes since the last write",
            L"Partition table...",
            L"System information",
            L"Theme...",
            L"About",
            L"Exit to dashboard",
        };

        bool enabled[MENU_ITEM_COUNT];
        for (int i = 0; i < MENU_ITEM_COUNT; i++)
        {
            enabled[i] = true;
        }
        enabled[MENU_FORMAT_SELECTED] = m_pTable->isPartitionEnabled(m_CurrentPart);
        // Checks what is on disk, i.e. the written table
        enabled[MENU_CHECK_SELECTED] = m_pTableOriginal->isPartitionEnabled(m_CurrentPart);
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
        case MENU_CHECK_SELECTED:
            DoCheckFileSystem();
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
        case MENU_ABOUT:
            DisplayAbout();
            return;
        case MENU_EXIT:
            if (ConfirmExit())
            {
                QuitToDashboard();
            }
            return;
        default:
            return;
        }
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

    const int original = m_Ui.GetThemeIndex();
    const int chosen = RunMenu(L"Theme", labels, enabled, count, original, true, &App::PreviewTheme);

    if (chosen < 0)
    {
        m_Ui.SetTheme(original);
        return chosen == MENU_RESULT_CLOSE;
    }

    m_Ui.SetTheme(chosen);
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

// Nothing is written. False if the user went back with B.
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

// Indexed by LoadedTable. Missing or broken tables can't be loaded (that
// would wipe the layout), the kernel's needs the patch. True if any can.
bool App::GetLoadableTables(bool loadable[LOADED_TABLE_COUNT])
{
    loadable[LOADED_TABLE_KERNEL] = (m_pKernelTable != NULL);
    loadable[LOADED_TABLE_HDD] = IsMainTableUsable();
    loadable[LOADED_TABLE_HDD_BACKUP] = IsBackupTableUsable();

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

// Nothing is written. An identical layout leaves the table alone, so the
// "Current table" label keeps its source.
void App::ApplyPreset(int preset)
{
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

// Only the editor changes (no confirmation): write it to make it active
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

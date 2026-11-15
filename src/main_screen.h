#ifndef MAIN_SCREEN_H_
#define MAIN_SCREEN_H_

#include <xtl.h>
#include "filesystem_cache.h"
#include "gamepad_input.h"
#include "layout.h"
#include "partition_table_session.h"
#include "partition_table_store.h"
#include "ui.h"

// Shown in the title bar and on the About screen
const wchar_t PROGRAM_NAME_AND_VERSION[] = L"XBpartitioner 2.0";

// What the main screen shows: they must outlive it
struct MainScreenSources
{
    const PartitionTableSession &session;
    const PartitionTableStore &tables;
    const FilesystemCache &filesystems;
};

// The partition table, disk map and free space, with the selected partition.
// Also drawn behind every other screen, every frame. In layout space (see ui.h).
class MainScreen
{
  public:
    MainScreen(Renderer &ui, const MainScreenSources &sources);
    // Once the video mode is known
    void Configure(const Layout *layout);

    // All but the controls hint
    void Draw();
    void DrawControlsHint();

    int SelectedPartition() const
    {
        return m_selected;
    }
    void Select(int partition);
    // Once per frame with the D-pad direction held (0: none), so a held
    // D-pad repeats
    void MoveSelection(int direction);
    // Once per frame, before drawing: the bar slides to the selected row
    void UpdateSelectionBar();

  private:
    // The disk map's inside, and the sectors it spans
    struct DiskMapArea
    {
        FLOAT left, right, top, bottom;
        ULONGLONG start, end;
        double pixels_per_sector;
    };

    void DrawTable();
    void DrawDiskMap();
    // Enabled and not empty
    bool IsOnDiskMap(int partition) const;
    int CountDiskMapPartitions() const;
    // How many partitions on the map start before it (ties: by number)
    int DiskMapPlace(int partition) const;
    void DrawDiskMapPartition(int partition, const DiskMapArea &area, D3DCOLOR color);
    void DrawFreeSpace();
    void DrawLoadedTableName();
    void FormatTableHeader(wchar_t *row);
    void FormatPartitionRow(const int partition_number, wchar_t *row);

    int TableCenterX() const;
    // Slot 0 is the column headers, then the line under them, then one pitch apart
    int TableSlotTop(int slot) const;
    // Slots of the section labels, and of partition i's row
    static int StandardLabelSlot();
    static int ExtendedLabelSlot();
    int TableRowTop(int partition) const;
    const Theme &Colors() const
    {
        return m_ui.GetTheme();
    }

    Renderer &m_ui;
    const PartitionTableSession &m_session;
    const PartitionTableStore &m_tables;
    const FilesystemCache &m_filesystems;
    const Layout *m_layout;

    int m_selected;
    FLOAT m_bar_y;
    bool m_bar_placed; // false until the first frame: start on the row, no slide
    DWORD m_bar_tick;  // For SlideTowards()
    DpadRepeat m_repeat;
};

#endif // MAIN_SCREEN_H_

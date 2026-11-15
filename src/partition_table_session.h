#ifndef PARTITION_TABLE_SESSION_H_
#define PARTITION_TABLE_SESSION_H_

#include "partition.hpp"
#include "partition_table_changes.h"

// Where the edited table came from
enum LoadedTable
{
    LOADED_TABLE_KERNEL = 0,
    LOADED_TABLE_HDD = 1,
    LOADED_TABLE_HDD_BACKUP = 2,
    LOADED_TABLE_COUNT, // Number of tables that can be loaded
    LOADED_TABLE_PRESET = LOADED_TABLE_COUNT
};

// Preset layouts, in the order of the New partition table menu
enum TablePreset
{
    PRESET_STOCK = 0,
    PRESET_F,   // F takes the rest of the disk
    PRESET_F_G, // F and G split it
    PRESET_COUNT
};

// The table being edited and the table in use it started from, so the
// screens know what changed. Nothing here is written to the disk.
class PartitionTableSession
{
  public:
    // Empty until Start(): no disk is known yet
    PartitionTableSession();

    // Both tables from this one (after reading or writing the tables): no changes
    void Start(const PartitionTableEditor &in_use);
    // The same, keeping this disk and mode: a written table becomes the one in use
    void Restart(const XboxPartitionTable &in_use);

    PartitionTableEditor &Edited()
    {
        return m_edited;
    }
    const PartitionTableEditor &Edited() const
    {
        return m_edited;
    }
    // The table in use, where the partitions are now
    const PartitionTableEditor &InUse() const
    {
        return m_in_use;
    }
    // Valid until either table changes
    PartitionTableChanges Changes() const
    {
        return PartitionTableChanges(m_edited, m_in_use);
    }

    LoadedTable Source() const
    {
        return m_source;
    }
    // Into the editor only
    void Load(const XboxPartitionTable &table, LoadedTable source);
    void UndoAllChanges();
    // Replaces the edited layout, unless the preset gives the same one
    void ApplyPreset(TablePreset preset);
    // A partition left empty is disabled: it would only block its spot
    void DisableIfEmpty(int partition);

  private:
    PartitionTableEditor m_edited;
    PartitionTableEditor m_in_use;
    LoadedTable m_source;
};

#endif // PARTITION_TABLE_SESSION_H_

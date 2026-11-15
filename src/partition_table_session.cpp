#include "partition_table_session.h"

PartitionTableSession::PartitionTableSession() : m_edited(0, false, false), m_in_use(0, false, false), m_source(LOADED_TABLE_KERNEL)
{
}

void PartitionTableSession::Start(const PartitionTableEditor &in_use)
{
    m_edited = in_use;
    m_in_use = in_use;
    m_source = LOADED_TABLE_KERNEL;
}

void PartitionTableSession::Restart(const XboxPartitionTable &in_use)
{
    m_edited.LoadNewTable(in_use);
    m_in_use.LoadNewTable(in_use);
    m_source = LOADED_TABLE_KERNEL;
}

void PartitionTableSession::Load(const XboxPartitionTable &table, LoadedTable source)
{
    m_edited.LoadNewTable(table);
    m_source = source;
}

void PartitionTableSession::UndoAllChanges()
{
    Load(m_in_use.GetPartitionTable(), LOADED_TABLE_KERNEL);
}

void PartitionTableSession::ApplyPreset(TablePreset preset)
{
    // The editor's setup for each preset, indexed by TablePreset
    typedef void (PartitionTableEditor::*PresetSetup)();
    static const PresetSetup setups[PRESET_COUNT] = {
        &PartitionTableEditor::SetupPartitionsStock,
        &PartitionTableEditor::SetupPartitionsFOnly,
        &PartitionTableEditor::SetupPartitionsFGSplit,
    };
    if (preset < 0 || preset >= PRESET_COUNT)
    {
        return;
    }
    PartitionTableEditor preview = m_edited;
    (preview.*setups[preset])();

    if (!preview.HasSameLayout(m_edited))
    {
        Load(preview.GetPartitionTable(), LOADED_TABLE_PRESET);
    }
}

void PartitionTableSession::DisableIfEmpty(int partition)
{
    if (m_edited.IsPartitionEnabled(partition) && m_edited.GetPartitionSize(partition) == 0)
    {
        m_edited.DisablePartition(partition);
    }
}

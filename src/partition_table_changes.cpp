#include "partition_table_changes.h"

PartitionTableChanges::PartitionTableChanges(const PartitionTableEditor &edited, const PartitionTableEditor &original)
    : m_edited(edited), m_original(original)
{
}

bool PartitionTableChanges::Any() const
{
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (IsAdded(i) || Affects(i))
        {
            return true;
        }
    }
    return false;
}

bool PartitionTableChanges::IsAdded(int partition) const
{
    return IsIndex(partition) && m_edited.IsPartitionEnabled(partition) && !m_original.IsPartitionEnabled(partition);
}

bool PartitionTableChanges::IsMovedOrResized(int partition) const
{
    return IsEnabledInBoth(partition) && (!HasSameStart(partition) || !HasSameSize(partition));
}

bool PartitionTableChanges::Affects(int partition) const
{
    return IsIndex(partition) && m_original.IsPartitionEnabled(partition) && (!m_edited.IsPartitionEnabled(partition) || IsMovedOrResized(partition));
}

bool PartitionTableChanges::IsResizedAtSameStart(int partition) const
{
    return IsEnabledInBoth(partition) && HasSameStart(partition) && !HasSameSize(partition);
}

PartitionSet PartitionTableChanges::Changed(const PartitionSet &kept) const
{
    PartitionSet changed;
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if ((IsAdded(i) || IsMovedOrResized(i)) && !kept.Contains(i))
        {
            changed.Add(i);
        }
    }
    return changed;
}

PartitionSet PartitionTableChanges::MovedOrResized(const PartitionSet &kept) const
{
    PartitionSet changed;
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (IsMovedOrResized(i) && !kept.Contains(i))
        {
            changed.Add(i);
        }
    }
    return changed;
}

PartitionSet PartitionTableChanges::Shrinking(const PartitionSet &partitions) const
{
    PartitionSet shrinking;
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (partitions.Contains(i) && m_edited.GetPartitionSize(i) < m_original.GetPartitionSize(i))
        {
            shrinking.Add(i);
        }
    }
    return shrinking;
}

PartitionSet PartitionTableChanges::Growing(const PartitionSet &partitions) const
{
    PartitionSet growing;
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (partitions.Contains(i) && m_edited.GetPartitionSize(i) > m_original.GetPartitionSize(i))
        {
            growing.Add(i);
        }
    }
    return growing;
}

bool PartitionTableChanges::IsIndex(int partition)
{
    return partition >= 0 && partition < (int)XboxPartitionTableEntryCount;
}

bool PartitionTableChanges::IsEnabledInBoth(int partition) const
{
    return IsIndex(partition) && m_edited.IsPartitionEnabled(partition) && m_original.IsPartitionEnabled(partition);
}

bool PartitionTableChanges::HasSameStart(int partition) const
{
    return m_edited.GetPartitionStart(partition) == m_original.GetPartitionStart(partition);
}

bool PartitionTableChanges::HasSameSize(int partition) const
{
    return m_edited.GetPartitionSize(partition) == m_original.GetPartitionSize(partition);
}

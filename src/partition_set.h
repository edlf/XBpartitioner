#ifndef PARTITION_SET_H_
#define PARTITION_SET_H_

#include "partition.hpp"

// A set of partition table indexes (0 to XboxPartitionTableEntryCount - 1),
// e.g. the partitions a write will format or the resizes that keep their data
class PartitionSet
{
  public:
    PartitionSet() : m_members(0)
    {
    }

    static PartitionSet Only(int partition)
    {
        PartitionSet set;
        set.Add(partition);
        return set;
    }

    // Ignores an index outside the table
    void Add(int partition)
    {
        if (IsIndex(partition))
        {
            m_members |= Bit(partition);
        }
    }

    bool Contains(int partition) const
    {
        return IsIndex(partition) && (m_members & Bit(partition)) != 0;
    }

    bool IsEmpty() const
    {
        return m_members == 0;
    }

    int Count() const
    {
        int count = 0;
        for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
        {
            count += Contains(i) ? 1 : 0;
        }
        return count;
    }

    PartitionSet Union(const PartitionSet &other) const
    {
        PartitionSet both;
        both.m_members = m_members | other.m_members;
        return both;
    }

  private:
    static bool IsIndex(int partition)
    {
        return partition >= 0 && partition < (int)XboxPartitionTableEntryCount;
    }

    static uint32_t Bit(int partition)
    {
        return (uint32_t)1 << partition;
    }

    uint32_t m_members;
};

#endif // PARTITION_SET_H_

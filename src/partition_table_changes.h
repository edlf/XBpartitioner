#ifndef PARTITION_TABLE_CHANGES_H_
#define PARTITION_TABLE_CHANGES_H_

#include "partition.hpp"
#include "partition_set.h"

// How an edited partition table differs from the one it was loaded from (the
// table on disk). Only reads both tables, which must outlive it.
class PartitionTableChanges
{
  public:
    PartitionTableChanges(const PartitionTableEditor &edited, const PartitionTableEditor &original);

    // Any partition enabled, disabled, moved or resized
    bool Any() const;

    // Enabled now, not in the original
    bool IsAdded(int partition) const;
    // Enabled in both, at another start or with another size
    bool IsMovedOrResized(int partition) const;
    // Was enabled, and is now disabled, moved or resized: its data is gone
    bool Affects(int partition) const;
    // Enabled in both at the same start with another size: a resize that may
    // keep its data
    bool IsResizedAtSameStart(int partition) const;

    // Added, moved or resized partitions, except those in kept
    PartitionSet Changed(const PartitionSet &kept) const;
    // Moved or resized partitions, except those in kept
    PartitionSet MovedOrResized(const PartitionSet &kept) const;
    // The partitions of these that get smaller, or bigger
    PartitionSet Shrinking(const PartitionSet &partitions) const;
    PartitionSet Growing(const PartitionSet &partitions) const;

  private:
    static bool IsIndex(int partition);
    bool IsEnabledInBoth(int partition) const;
    bool HasSameStart(int partition) const;
    bool HasSameSize(int partition) const;

    const PartitionTableEditor &m_edited;
    const PartitionTableEditor &m_original;
};

#endif // PARTITION_TABLE_CHANGES_H_

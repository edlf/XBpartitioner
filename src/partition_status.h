#ifndef PARTITION_STATUS_H_
#define PARTITION_STATUS_H_

#include "partition.hpp"
#include "partition_usage.h"

// How the editor shows a status: grey, yellow or green
enum StatusTone
{
    STATUS_TONE_OFF,     // Nothing there
    STATUS_TONE_CHANGE,  // Something happens to it when written
    STATUS_TONE_KEEPS    // Its data is safe
};

const size_t PARTITION_STATUS_TEXT_COUNT = 80;

// What happens to one partition when the edited table is written
struct PartitionStatus
{
    wchar_t text[PARTITION_STATUS_TEXT_COUNT];
    StatusTone tone;
};

// What DescribePartitionStatus() looks at: they must outlive the call
struct PartitionStatusQuery
{
    const PartitionTableEditor &edited;
    const PartitionTableEditor &in_use;
    const PartitionUsage &usage;  // Of this partition on disk, if known
    bool resize_keeps_data;       // The setting: off, every resize formats
};

// Disabled, new, removed, unchanged, or changed: then whether a resize keeps
// its data and, if not, the nearest sizes that would. Errors are only found
// by the check when writing.
PartitionStatus DescribePartitionStatus(const PartitionStatusQuery &query, int partition);

#endif // PARTITION_STATUS_H_

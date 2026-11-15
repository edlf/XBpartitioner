#include "partition_status.h"
#include "partition_format.h"

namespace
{

PartitionStatus Status(StatusTone tone, const wchar_t *text)
{
    PartitionStatus status;
    status.tone = tone;
    FormatWide(status.text, PARTITION_STATUS_TEXT_COUNT, L"%s", text);
    return status;
}

// A format is needed; suggest the nearest sizes that keep the data and fit
// the free space
PartitionStatus SuggestKeepingSizes(const PartitionStatusQuery &query, int partition)
{
    const uint64_t old_size = query.in_use.GetPartitionSize(partition);
    const uint64_t size = query.edited.GetPartitionSize(partition);
    const uint64_t below = FatxKeepingSizeBelow(old_size, query.usage.Get(), size);
    uint64_t above = FatxKeepingSizeAbove(old_size, query.usage.Get(), size);
    if (above > size + query.edited.GetGrowthLimit(partition))
    {
        above = 0;
    }
    if (below == 0 && above == 0)
    {
        return Status(STATUS_TONE_CHANGE, L"changed, unreadable until formatted");
    }

    wchar_t below_size[HUMAN_SIZE_TEXT_COUNT];
    wchar_t above_size[HUMAN_SIZE_TEXT_COUNT];
    FormatHumanSize(below, below_size);
    FormatHumanSize(above, above_size);
    PartitionStatus status = Status(STATUS_TONE_CHANGE, L"");
    if (below != 0 && above != 0)
    {
        FormatWide(status.text, PARTITION_STATUS_TEXT_COUNT, L"format needed (data kept: %s or %s)", below_size, above_size);
    }
    else
    {
        FormatWide(status.text, PARTITION_STATUS_TEXT_COUNT, L"format needed (data kept: %s)", below != 0 ? below_size : above_size);
    }
    return status;
}

// Moved or resized: with the same start, some sizes keep the data, depending
// on how much is in use
PartitionStatus DescribeChange(const PartitionStatusQuery &query, int partition)
{
    const uint64_t old_size = query.in_use.GetPartitionSize(partition);
    const uint64_t size = query.edited.GetPartitionSize(partition);
    const bool same_start = query.edited.GetPartitionStart(partition) == query.in_use.GetPartitionStart(partition);
    const bool formatted = query.resize_keeps_data && same_start && query.usage.IsKnown();
    const FatxResizeKind kind = formatted ? FatxClassifyResize(old_size, query.usage.Get(), size) : FATX_RESIZE_KIND_FORMAT;

    if (kind == FATX_RESIZE_KIND_IN_PLACE)
    {
        return Status(STATUS_TONE_KEEPS, L"resized, keeps its data");
    }
    if (kind == FATX_RESIZE_KIND_MOVES)
    {
        return Status(STATUS_TONE_KEEPS, L"resized, keeps its data (clusters move)");
    }
    if (formatted)
    {
        return SuggestKeepingSizes(query, partition);
    }
    return Status(STATUS_TONE_CHANGE, L"changed, unreadable until formatted");
}

} // namespace

PartitionStatus DescribePartitionStatus(const PartitionStatusQuery &query, int partition)
{
    const bool enabled = query.edited.IsPartitionEnabled(partition);
    const bool was_enabled = query.in_use.IsPartitionEnabled(partition);
    if (!enabled && !was_enabled)
    {
        return Status(STATUS_TONE_OFF, L"disabled");
    }
    if (enabled && !was_enabled)
    {
        return Status(STATUS_TONE_CHANGE, L"new, format it with write and format");
    }
    if (!enabled)
    {
        return Status(STATUS_TONE_CHANGE, L"will be removed when written");
    }

    const bool moved = query.edited.GetPartitionStart(partition) != query.in_use.GetPartitionStart(partition) ||
                       query.edited.GetPartitionSize(partition) != query.in_use.GetPartitionSize(partition);
    if (moved)
    {
        return DescribeChange(query, partition);
    }
    return Status(STATUS_TONE_KEEPS, L"unchanged");
}

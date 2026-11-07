#include "partition.hpp"
#include <string.h>

XboxPartitionTableManipulator::XboxPartitionTableManipulator(const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k) : m_hdd_lba_size(total_hdd_lba), m_lba_v4(lbav4), m_physical_4k(physical_4k)
{
    SetupMaxSize();
    SetupPartitionsStock();
}

XboxPartitionTableManipulator::XboxPartitionTableManipulator(const XboxPartitionTable &part_table, const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k) : m_table(part_table), m_hdd_lba_size(total_hdd_lba), m_lba_v4(lbav4), m_physical_4k(physical_4k)
{

    SetupMaxSize();

    if (!HasMagic())
    {
        SetupPartitionsStock();
    }
    else
    {
        DropHighLbaBitsIfNotV4();
    }
}

// Pre v4 patches ignore the high LBA bits: cleared so the editor matches the
// kernel. HasHighLbaBits() on the original table tells whether to warn.
void XboxPartitionTableManipulator::DropHighLbaBitsIfNotV4()
{
    if (m_lba_v4)
    {
        return;
    }

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        m_table.TableEntries[i].LBAStart_high = 0;
        m_table.TableEntries[i].LBASize_high = 0;
    }
}

bool XboxPartitionTableManipulator::IsTableUsable(const XboxPartitionTable &part_table, const uint64_t total_sectors)
{
    if (!HasMagic(part_table))
    {
        return false;
    }

    int in_use = 0;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const XboxPartitionTableEntry &entry = part_table.TableEntries[i];
        if (entry.Flags != PE_PARTFLAGS_IN_USE)
        {
            continue;
        }
        in_use++;

        const uint64_t start = (uint64_t)entry.LBAStart_high << 32 | entry.LBAStart;
        const uint64_t end = start + ((uint64_t)entry.LBASize_high << 32 | entry.LBASize);

        if (end <= start || start < XBOX_SWAPPART1_LBA_START || (total_sectors != 0 && end > total_sectors))
        {
            return false;
        }

        for (int j = i + 1; j < XboxPartitionTableEntryCount; j++)
        {
            const XboxPartitionTableEntry &other = part_table.TableEntries[j];
            if (other.Flags != PE_PARTFLAGS_IN_USE)
            {
                continue;
            }

            const uint64_t other_start = (uint64_t)other.LBAStart_high << 32 | other.LBAStart;
            const uint64_t other_end = other_start + ((uint64_t)other.LBASize_high << 32 | other.LBASize);
            if (start < other_end && other_start < end)
            {
                return false;
            }
        }
    }

    return in_use > 0;
}

bool XboxPartitionTableManipulator::FindConflict(int *first, int *second) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!isPartitionEnabled(i))
        {
            continue;
        }
        const uint64_t start = GetPartitionStart(i);
        const uint64_t end = start + GetPartitionSize(i);

        for (int j = i + 1; j < XboxPartitionTableEntryCount; j++)
        {
            if (!isPartitionEnabled(j))
            {
                continue;
            }
            const uint64_t other_start = GetPartitionStart(j);
            const uint64_t other_end = other_start + GetPartitionSize(j);

            if (start == other_start || (start < other_end && other_start < end))
            {
                *first = i;
                *second = j;
                return true;
            }
        }
    }

    return false;
}

bool XboxPartitionTableManipulator::FindPartitionBeforeUsableStart(int *partition) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (isPartitionEnabled(i) && GetPartitionStart(i) < XBOX_SWAPPART1_LBA_START)
        {
            *partition = i;
            return true;
        }
    }

    return false;
}

bool XboxPartitionTableManipulator::HasHighLbaBits(const XboxPartitionTable &part_table)
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const XboxPartitionTableEntry &entry = part_table.TableEntries[i];

        // The kernel skips the others
        if (entry.Flags == PE_PARTFLAGS_IN_USE && (entry.LBAStart_high != 0 || entry.LBASize_high != 0))
        {
            return true;
        }
    }

    return false;
}

void XboxPartitionTableManipulator::SetupMaxSize()
{
    // v3 and earlier address 32 bit LBAs
    uint64_t total = m_hdd_lba_size;
    if (!m_lba_v4 && total > (uint64_t)MAX_HDD_SIZE_LBA48_V3)
    {
        total = MAX_HDD_SIZE_LBA48_V3;
    }

    m_last_sector_lba = (total >= RESERVED_SPACE_END_LBA) ? total - RESERVED_SPACE_END_LBA : 0;

    // See RESERVED_SPACE_END_LBA
    uint64_t backup_lba;
    if (m_physical_4k)
    {
        const uint64_t last_full_4k_block_end = total & ~(uint64_t)(LBA_ALIGNMENT - 1);
        backup_lba = (last_full_4k_block_end >= LBA_ALIGNMENT) ? last_full_4k_block_end - LBA_ALIGNMENT : 0;
    }
    else
    {
        backup_lba = m_last_sector_lba;
    }

    if (backup_lba >= XBOX_STANDARD_MAX_LBA)
    {
        m_backup_table_lba = backup_lba;
        m_last_usable_lba = backup_lba;
    }
    else
    {
        // Stock 8GB drive (it ends at XBOX_STANDARD_MAX_LBA) or unknown size: no
        // backup, it would be E's last sector
        m_backup_table_lba = 0;
        m_last_usable_lba = total;
    }

    m_usable_lba_size = (m_last_usable_lba > XBOX_SWAPPART1_LBA_START) ? m_last_usable_lba - XBOX_SWAPPART1_LBA_START : 0;
}

uint64_t XboxPartitionTableManipulator::GetBackupTableLba() const
{
    return m_backup_table_lba;
}

uint64_t XboxPartitionTableManipulator::GetLastSectorLba() const
{
    return m_last_sector_lba;
}

bool XboxPartitionTableManipulator::OverlapsBackupTable() const
{
    return m_backup_table_lba != 0 && OverlapsSector(m_backup_table_lba);
}

bool XboxPartitionTableManipulator::OverlapsSector(const uint64_t lba) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (isPartitionEnabled(i) && GetPartitionStart(i) <= lba && lba < GetPartitionStart(i) + GetPartitionSize(i))
        {
            return true;
        }
    }

    return false;
}

void XboxPartitionTableManipulator::SetPartitionName(const uint16_t partition_number, const char *name)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    // Space padded, cut at XboxPartitionNameMaxLength
    const size_t length = strlen(name);
    for (size_t i = 0; i < XboxPartitionNameMaxLength; i++)
    {
        m_table.TableEntries[partition_number].Name[i] = (i < length) ? name[i] : ' ';
    }
}

void XboxPartitionTableManipulator::SetPartitionSize(const int partition_number, const uint64_t value)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    const uint64_t aligned_lba = AlignTo4k(value);
    m_table.TableEntries[partition_number].LBASize = (uint32_t)(aligned_lba & LBA_LOWER_BITS);
    m_table.TableEntries[partition_number].LBASize_high = (uint16_t)(aligned_lba >> 32);
}

void XboxPartitionTableManipulator::SetPartitionStart(const int partition_number, const uint64_t value)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    const uint64_t lba_aligned = AlignStartTo4k(value);
    m_table.TableEntries[partition_number].LBAStart = (uint32_t)(lba_aligned & LBA_LOWER_BITS);
    m_table.TableEntries[partition_number].LBAStart_high = (uint16_t)(lba_aligned >> 32);
}

uint64_t XboxPartitionTableManipulator::GetPartitionSize(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    return (uint64_t)m_table.TableEntries[partition_number].LBASize_high << 32 | m_table.TableEntries[partition_number].LBASize;
}

uint64_t XboxPartitionTableManipulator::GetPartitionStart(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    return (uint64_t)m_table.TableEntries[partition_number].LBAStart_high << 32 | m_table.TableEntries[partition_number].LBAStart;
}

unsigned int XboxPartitionTableManipulator::CalculateClusterSizeForPartition(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    return CalculateClusterSize(GetPartitionSize(partition_number));
}

unsigned int XboxPartitionTableManipulator::CalculateClusterSize(const uint64_t lba_size) const
{
    uint32_t clusterSize = 16;
    uint64_t compare = 0x20000000;
    while (lba_size > compare)
    {
        compare = compare * 2;
        clusterSize = clusterSize * 2;
    }
    return clusterSize;
}

void XboxPartitionTableManipulator::SortPartitions(uint64_t *starts, uint64_t *ends) const
{
    // Disabled entries take no space: 0, so they sort first and are skipped
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (isPartitionEnabled(i))
        {
            starts[i] = GetPartitionStart(i);
            ends[i] = GetPartitionStart(i) + GetPartitionSize(i);
        }
        else
        {
            starts[i] = 0;
            ends[i] = 0;
        }
    }

    // Insertion sort by start, ends kept alongside
    for (int i = 1; i < XboxPartitionTableEntryCount; i++)
    {
        const uint64_t start = starts[i];
        const uint64_t end = ends[i];
        int j = i;
        while (j > 0 && starts[j - 1] > start)
        {
            starts[j] = starts[j - 1];
            ends[j] = ends[j - 1];
            j--;
        }
        starts[j] = start;
        ends[j] = end;
    }
}

int XboxPartitionTableManipulator::GetActivePartitionCount() const
{
    int count = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (isPartitionEnabled(i))
        {
            count++;
        }
    }

    return count;
}

uint64_t XboxPartitionTableManipulator::GetFirstFreeStart() const
{
    uint64_t starts[XboxPartitionTableEntryCount], ends[XboxPartitionTableEntryCount];

    if (GetActivePartitionCount() == 0)
    {
        return XBOX_SWAPPART1_LBA_START;
    }

    SortPartitions(starts, ends);

    // First gap with room for a 4 KB block once the start is aligned
    for (int i = 0; i < (XboxPartitionTableEntryCount - 1); i++)
    {
        if (!starts[i])
        {
            continue;
        }

        const uint64_t candidate = AlignStartTo4k(ends[i]);
        if (candidate + LBA_ALIGNMENT <= starts[i + 1])
        {
            return candidate;
        }
    }

    const uint64_t candidate = AlignStartTo4k(ends[(XboxPartitionTableEntryCount - 1)]);
    if (candidate + LBA_ALIGNMENT <= m_last_usable_lba)
    {
        return candidate;
    }

    return 0;
}

uint64_t XboxPartitionTableManipulator::GetAvailableFreeSpaceForPartition(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    // Disabled: no space to grow into. Never placed (0): must not grow from LBA 0.
    const uint64_t part_start = GetPartitionStart(partition_number);
    if (!isPartitionEnabled(partition_number) || part_start == 0)
    {
        return 0;
    }

    // Compared by index, so one sharing this start is never mistaken for it
    const uint64_t part_end = part_start + GetPartitionSize(partition_number);
    uint64_t next_start = m_last_usable_lba;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (i == partition_number || !isPartitionEnabled(i))
        {
            continue;
        }

        const uint64_t other_start = GetPartitionStart(i);
        const uint64_t other_end = other_start + GetPartitionSize(i);
        if (other_end <= part_start && other_start < part_start)
        {
            continue; // Before this one
        }
        if (other_start < part_end || other_start < part_start)
        {
            // Overlapping: no room until that is fixed
            return 0;
        }
        if (other_start < next_start)
        {
            next_start = other_start;
        }
    }

    return (next_start > part_end) ? AlignTo4k(next_start - part_end) : 0;
}

uint64_t XboxPartitionTableManipulator::GetMaxPartitionSize() const
{
    return m_lba_v4 ? ~(uint64_t)0 : (uint64_t)LBASIZE_LBA48_V3_MAX;
}

unsigned int XboxPartitionTableManipulator::GetMaxClusterSize() const
{
    return m_lba_v4 ? 0 : LBA48_V3_MAX_CLUSTER_KB;
}

// 0 when at or over it (a v4 table on v3 can only shrink)
static uint64_t room_below_max(uint64_t size, uint64_t max_size)
{
    return (size < max_size) ? max_size - size : 0;
}

uint64_t XboxPartitionTableManipulator::GetGrowthLimit(const int partition_number) const
{
    const uint64_t free_after = GetAvailableFreeSpaceForPartition(partition_number);
    const uint64_t room = room_below_max(GetPartitionSize(partition_number), GetMaxPartitionSize());
    return (free_after < room) ? free_after : room;
}

uint64_t XboxPartitionTableManipulator::GetGrowthLimitBefore(const int partition_number) const
{
    const uint64_t free_before = GetAvailableFreeSpaceForPartitionBefore(partition_number);
    // Both 4K aligned, so moving the start back by this keeps it aligned
    const uint64_t room = AlignTo4k(room_below_max(GetPartitionSize(partition_number), GetMaxPartitionSize()));
    return (free_before < room) ? free_before : room;
}

void XboxPartitionTableManipulator::PartitionIncreaseSizeBy(const int partition_number, uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    uint64_t max = GetGrowthLimit(partition_number);

    if (delta > max)
    {
        delta = max;
    }

    uint64_t part_size = GetPartitionSize(partition_number);
    SetPartitionSize(partition_number, part_size + delta);
}

void XboxPartitionTableManipulator::PartitionDecreaseSizeBy(const int partition_number, uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    uint64_t part_size = GetPartitionSize(partition_number);
    if (part_size <= delta)
    {
        SetPartitionSize(partition_number, 0);
    }
    else
    {
        SetPartitionSize(partition_number, part_size - delta);
    }
}

void XboxPartitionTableManipulator::EnablePartition(const int partition_number)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    if (isPartitionEnabled(partition_number))
    {
        return;
    }

    // Never placed (0), or its old range was taken since: placed empty at the
    // first free spot
    const uint64_t start = GetPartitionStart(partition_number);
    const uint64_t end = start + GetPartitionSize(partition_number);
    if (start == 0 || !IsRangeFree(start, end))
    {
        const uint64_t free_start = GetFirstFreeStart();
        if (free_start == 0)
        {
            // Disk full: stays disabled
            return;
        }
        SetPartitionStart(partition_number, free_start);
        SetPartitionSize(partition_number, 0);
    }

    // Extended partitions made here get the same style of name as the presets
    if (partition_number >= XBOX_STANDARD_PARTITION_COUNT && IsNameBlank(partition_number))
    {
        char name[] = "XBOX EXTRA F";
        name[sizeof(name) - 2] = (char)('F' + (partition_number - XBOX_STANDARD_PARTITION_COUNT));
        SetPartitionName(partition_number, name);
    }

    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_IN_USE;
}

// Inside the usable area and not used by an enabled partition
bool XboxPartitionTableManipulator::IsRangeFree(const uint64_t start, const uint64_t end) const
{
    if (start < XBOX_SWAPPART1_LBA_START || end > m_last_usable_lba || end < start)
    {
        return false;
    }

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!isPartitionEnabled(i))
        {
            continue;
        }

        const uint64_t other_start = GetPartitionStart(i);
        const uint64_t other_end = other_start + GetPartitionSize(i);
        if (start < other_end && other_start < end)
        {
            return false;
        }
    }

    return true;
}

// For the presets' fixed layouts: no placement
void XboxPartitionTableManipulator::MarkEnabled(const int partition_number)
{
    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_IN_USE;
}

bool XboxPartitionTableManipulator::IsNameBlank(const int partition_number) const
{
    for (size_t i = 0; i < XboxPartitionNameMaxLength; i++)
    {
        const unsigned char c = m_table.TableEntries[partition_number].Name[i];
        if (c != ' ' && c != '\0')
        {
            return false;
        }
    }

    return true;
}

void XboxPartitionTableManipulator::DisablePartition(const int partition_number)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_EMPTY;
}

bool XboxPartitionTableManipulator::isPartitionEnabled(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return false;
    }

    return (m_table.TableEntries[partition_number].Flags == PE_PARTFLAGS_IN_USE);
}

void XboxPartitionTableManipulator::ErasePartition(const int partition_number)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    SetPartitionName(partition_number, "");
    SetPartitionStart(partition_number, 0);
    SetPartitionSize(partition_number, 0);
    m_table.TableEntries[partition_number].Flags = 0x0;
}

int XboxPartitionTableManipulator::GetFirstAvailablePartitionSlot() const
{
    int n = XboxPartitionTableEntryCount - 1;

    for (int i = 0; i <= n; i++)
    {
        if (!isPartitionEnabled(i))
        {
            return i;
        }
    }

    return n;
}

uint64_t XboxPartitionTableManipulator::GetAvailableFreeSpaceForPartitionBefore(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    const uint64_t part_start = GetPartitionStart(partition_number);

    if (!isPartitionEnabled(partition_number) || part_start <= XBOX_SWAPPART1_LBA_START)
    {
        return 0;
    }

    // The previous partition is the latest end among those starting before
    uint64_t previous_end = XBOX_SWAPPART1_LBA_START;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (i == partition_number || !isPartitionEnabled(i))
        {
            continue;
        }

        const uint64_t other_start = GetPartitionStart(i);
        const uint64_t other_end = other_start + GetPartitionSize(i);
        if (other_start > part_start)
        {
            continue; // After this one
        }
        // An empty partition at this same start takes no space in front of it
        if (other_start == part_start && other_end == part_start)
        {
            continue;
        }
        if (other_end > part_start)
        {
            // Overlapping: moving back would make it worse
            return 0;
        }
        if (other_end > previous_end)
        {
            previous_end = other_end;
        }
    }

    // Rounded down, so it never overlaps the previous partition
    return (part_start > previous_end) ? AlignTo4k(part_start - previous_end) : 0;
}

void XboxPartitionTableManipulator::PartitionMoveBack(const int partition_number, uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    uint64_t free_space = GetAvailableFreeSpaceForPartitionBefore(partition_number);

    if (delta > free_space)
    {
        delta = free_space;
    }

    // Aligning an unaligned start up with no room before would move it forward
    const uint64_t part_start = GetPartitionStart(partition_number);
    const uint64_t new_start = AlignStartTo4k(part_start - delta);
    if (new_start < part_start)
    {
        SetPartitionStart(partition_number, new_start);
    }
}

void XboxPartitionTableManipulator::PartitionMoveForward(const int partition_number, uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    const uint64_t max = GetAvailableFreeSpaceForPartition(partition_number);

    if (delta > max)
    {
        delta = max;
    }

    // Rounding an unaligned start up could pass the free space: round down then
    const uint64_t part_start = GetPartitionStart(partition_number);
    uint64_t new_start = AlignStartTo4k(part_start + delta);
    if (new_start - part_start > max)
    {
        new_start = AlignTo4k(part_start + max);
    }

    if (new_start > part_start)
    {
        SetPartitionStart(partition_number, new_start);
    }
}

void XboxPartitionTableManipulator::PartitionStartForward(const int partition_number, const uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    const uint64_t part_start = GetPartitionStart(partition_number);
    const uint64_t part_size = GetPartitionSize(partition_number);

    if (part_start == 0)
    {
        return;
    }

    // A zero size partition moves instead
    if (part_size == 0)
    {
        PartitionMoveForward(partition_number, delta);
        return;
    }

    // Keeps at least one 4K block. Start aligned up and size down, so the end
    // never moves out.
    const uint64_t part_end = part_start + part_size;
    if (part_size <= LBA_ALIGNMENT)
    {
        return;
    }
    const uint64_t latest_start = AlignTo4k(part_end - LBA_ALIGNMENT);

    uint64_t new_start = AlignStartTo4k(part_start + delta);
    if (new_start > latest_start)
    {
        new_start = latest_start;
    }

    if (new_start > part_start)
    {
        SetPartitionStart(partition_number, new_start);
        SetPartitionSize(partition_number, part_end - new_start);
    }
}

void XboxPartitionTableManipulator::PartitionStartBack(const int partition_number, uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    const uint64_t part_start = GetPartitionStart(partition_number);
    if (part_start == 0)
    {
        return;
    }

    // A zero size partition moves instead
    if (GetPartitionSize(partition_number) == 0)
    {
        PartitionMoveBack(partition_number, delta);
        return;
    }

    // Grows: limited by the free space before and the maximum size
    const uint64_t free_space = GetGrowthLimitBefore(partition_number);
    if (delta > free_space)
    {
        delta = free_space;
    }

    // Aligned up, so it never reaches into the previous partition
    const uint64_t part_end = part_start + GetPartitionSize(partition_number);
    const uint64_t new_start = AlignStartTo4k(part_start - delta);

    if (new_start < part_start)
    {
        SetPartitionStart(partition_number, new_start);
        SetPartitionSize(partition_number, part_end - new_start);
    }
}

void XboxPartitionTableManipulator::MovePartitionStartTo(const int partition_number, const uint64_t start)
{
    const uint64_t part_start = GetPartitionStart(partition_number);

    if (start > part_start)
    {
        PartitionStartForward(partition_number, start - part_start);
    }
    else if (start < part_start)
    {
        PartitionStartBack(partition_number, part_start - start);
    }
}

void XboxPartitionTableManipulator::ResizePartitionTo(const int partition_number, const uint64_t size)
{
    const uint64_t part_size = GetPartitionSize(partition_number);

    if (size > part_size)
    {
        PartitionIncreaseSizeBy(partition_number, size - part_size);
    }
    else if (size < part_size)
    {
        PartitionDecreaseSizeBy(partition_number, part_size - size);
    }
}

uint64_t XboxPartitionTableManipulator::GetAvailableLBAs() const
{
    uint64_t used = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (isPartitionEnabled(i))
        {
            used += GetPartitionSize(i);
        }
    }

    if (used >= m_usable_lba_size)
    {
        return 0;
    }
    else
    {
        return m_usable_lba_size - used;
    }
}

void XboxPartitionTableManipulator::SetupPartitionsStock()
{
    WriteMagic();

    for (int i = 0; i < XboxPartitionTableHeaderReservedSize; i++)
    {
        m_table.Reserved[i] = 0;
    }

    // Magic bit, TODO figure out what this is
    m_table.Reserved[0] = 0x01;

    for (int i = 0; i < XboxPartitionTableFooterSize; i++)
    {
        m_table.Footer[i] = 0;
    }

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        ErasePartition(i);
    }

    SetPartitionStart(0, XBOX_DATAPART_LBA_START);
    SetPartitionSize(0, XBOX_DATAPART_LBA_SIZE);
    SetPartitionName(0, LBA48_Partition_Name_E);
    MarkEnabled(0);
    SetPartitionStart(1, XBOX_SYSPART_LBA_START);
    SetPartitionSize(1, XBOX_SYSPART_LBA_SIZE);
    SetPartitionName(1, LBA48_Partition_Name_C);
    MarkEnabled(1);
    SetPartitionStart(2, XBOX_SWAPPART1_LBA_START);
    SetPartitionSize(2, XBOX_SWAPPART_LBA_SIZE);
    SetPartitionName(2, LBA48_Partition_Name_X);
    MarkEnabled(2);
    SetPartitionStart(3, XBOX_SWAPPART2_LBA_START);
    SetPartitionSize(3, XBOX_SWAPPART_LBA_SIZE);
    SetPartitionName(3, LBA48_Partition_Name_Y);
    MarkEnabled(3);
    SetPartitionStart(4, XBOX_SWAPPART3_LBA_START);
    SetPartitionSize(4, XBOX_SWAPPART_LBA_SIZE);
    SetPartitionName(4, LBA48_Partition_Name_Z);
    MarkEnabled(4);
}

uint64_t XboxPartitionTableManipulator::PresetMaxPartitionSize() const
{
    const uint64_t max = GetMaxPartitionSize();
    return (max < (uint64_t)LBASIZE_16TB) ? max : (uint64_t)LBASIZE_16TB;
}

void XboxPartitionTableManipulator::SetupPartitionsFOnly()
{
    SetupPartitionsStock();

    // Stock sized drive, no room for F
    if (m_last_usable_lba <= XBOX_PART6_LBA_START)
    {
        return;
    }

    // Capped at 16TB, 1TB on v3
    SetPartitionStart(5, XBOX_PART6_LBA_START);
    const uint64_t cap = PresetMaxPartitionSize();
    const uint64_t space = m_last_usable_lba - GetPartitionStart(5);
    SetPartitionSize(5, (space > cap) ? cap : space);

    SetPartitionName(5, LBA48_Partition_Name_F);
    MarkEnabled(5);
}

void XboxPartitionTableManipulator::SetupPartitionsFGSplit()
{
    SetupPartitionsStock();

    // Stock sized drive, no room for F and G
    if (m_last_usable_lba <= XBOX_PART6_LBA_START + (2 * LBA_ALIGNMENT))
    {
        return;
    }

    // F gets half the space after the stock partitions, G the rest, each capped
    const uint64_t cap = PresetMaxPartitionSize();
    const uint64_t half = (m_last_usable_lba - XBOX_PART6_LBA_START) / 2;

    SetPartitionStart(5, XBOX_PART6_LBA_START);
    SetPartitionSize(5, (half > cap) ? cap : half);
    SetPartitionName(5, LBA48_Partition_Name_F);
    MarkEnabled(5);

    SetPartitionStart(6, GetPartitionStart(5) + GetPartitionSize(5));
    const uint64_t rest = m_last_usable_lba - GetPartitionStart(6);
    SetPartitionSize(6, (rest > cap) ? cap : rest);
    SetPartitionName(6, LBA48_Partition_Name_G);
    MarkEnabled(6);
}

XboxPartitionTable XboxPartitionTableManipulator::GetPartitionTable()
{
    return m_table;
}

void XboxPartitionTableManipulator::LoadNewTable(const XboxPartitionTable &part_table)
{
    m_table = part_table;
    DropHighLbaBitsIfNotV4();
}

bool XboxPartitionTableManipulator::HasMagic(const XboxPartitionTable &part_table)
{
    for (size_t i = 0; i < XboxPartitionTableMagicSize; i++)
    {
        if (part_table.Magic[i] != LBA48_Partition_Table_Magic[i])
        {
            return false;
        }
    }
    return true;
}

bool XboxPartitionTableManipulator::HasMagic() const
{
    return HasMagic(m_table);
}

void XboxPartitionTableManipulator::WriteMagic()
{
    for (size_t i = 0; i < XboxPartitionTableMagicSize; i++)
    {
        m_table.Magic[i] = LBA48_Partition_Table_Magic[i];
    }
}

XboxPartitionTableEntry XboxPartitionTableManipulator::getPartitionEntry(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        XboxPartitionTableEntry dummy = {0};
        return dummy;
    }

    return m_table.TableEntries[partition_number];
}

bool XboxPartitionTableManipulator::HasSameLayout(const XboxPartitionTableManipulator &other) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const bool enabled = isPartitionEnabled(i);
        if (enabled != other.isPartitionEnabled(i))
        {
            return false;
        }

        if (enabled && (GetPartitionStart(i) != other.GetPartitionStart(i) || GetPartitionSize(i) != other.GetPartitionSize(i)))
        {
            return false;
        }
    }

    return true;
}

bool XboxPartitionTableManipulator::RestorePartitionEntry(const int partition_number, const XboxPartitionTableEntry &entry)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return false;
    }

    // Check the saved range against the other partitions only, not this one
    const XboxPartitionTableEntry current = m_table.TableEntries[partition_number];
    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_EMPTY;

    if (entry.Flags == PE_PARTFLAGS_IN_USE)
    {
        const uint64_t start = (uint64_t)entry.LBAStart_high << 32 | entry.LBAStart;
        const uint64_t end = start + ((uint64_t)entry.LBASize_high << 32 | entry.LBASize);
        if (!IsRangeFree(start, end))
        {
            m_table.TableEntries[partition_number] = current;
            return false;
        }
    }

    m_table.TableEntries[partition_number] = entry;
    return true;
}

uint64_t XboxPartitionTableManipulator::GetUsableLBAs() const
{
    return m_usable_lba_size;
}

uint64_t XboxPartitionTableManipulator::AlignStartTo4k(const uint64_t value) const
{
    const uint64_t mask = LBA_ALIGNMENT_MASK;
    uint64_t aligned = value & mask;

    // Rounded up, so it never moves into the partition before
    if (value != aligned)
    {
        aligned += LBA_ALIGNMENT;
    }

    return aligned;
}

uint64_t XboxPartitionTableManipulator::AlignTo4k(const uint64_t value) const
{
    const uint64_t mask = LBA_ALIGNMENT_MASK;
    return value & mask;
}

bool XboxPartitionTableManipulator::isAlignedTo4k(const uint64_t value) const
{
    return value == AlignTo4k(value);
}

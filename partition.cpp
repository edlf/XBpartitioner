#include "partition.hpp"
#include <cstring>

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

// Pre v4 lba48 patches only know 32 bit LBAs: the fields v4 uses for the high
// 16 bits are reserved there and ignored by the kernel. Clear them so the
// editor (and anything written back) matches what the kernel actually uses.
// Callers can check HasHighLbaBits() on the original table to warn about it.
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

bool XboxPartitionTableManipulator::HasHighLbaBits(const XboxPartitionTable &part_table)
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const XboxPartitionTableEntry &entry = part_table.TableEntries[i];

        // Only partitions in use matter, the kernel skips the others
        if (entry.Flags == PE_PARTFLAGS_IN_USE && (entry.LBAStart_high != 0 || entry.LBASize_high != 0))
        {
            return true;
        }
    }

    return false;
}

void XboxPartitionTableManipulator::SetupMaxSize()
{
    // Cap to what lba48 v3 and earlier can address (32 bit LBAs)
    uint64_t total = m_hdd_lba_size;
    if (!m_lba_v4 && total > (uint64_t)MAX_HDD_SIZE_LBA48_V3)
    {
        total = MAX_HDD_SIZE_LBA48_V3;
    }

    m_last_sector_lba = (total >= RESERVED_SPACE_END_LBA) ? total - RESERVED_SPACE_END_LBA : 0;

    // Backup table location, see the comment at RESERVED_SPACE_END_LBA
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
        // Partitions end before the backup table
        m_backup_table_lba = backup_lba;
        m_last_usable_lba = backup_lba;
    }
    else
    {
        // No room after the stock partitions (a stock 8GB drive ends exactly at
        // XBOX_STANDARD_MAX_LBA), or the disk size is unknown: no backup table,
        // the backup sector would be the last sector of E
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
    // 0: no backup table on this disk
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

void XboxPartitionTableManipulator::SetPartitionName(const uint16_t partition_number, const std::string &name)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    for (size_t i = 0; i < XboxPartitionNameMaxLength; i++)
    {
        if (i < name.size())
        {
            m_table.TableEntries[partition_number].Name[i] = name[i];
        }
        else
        {
            m_table.TableEntries[partition_number].Name[i] = ' ';
        }
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

uint64_t XboxPartitionTableManipulator::GetPartitionSizeInBytes(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    return ((uint64_t)GetPartitionSize(partition_number) * HDD_SECTOR_SIZE);
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
    // Disabled entries keep their old start/size but don't occupy any space:
    // treat them like empty slots (0), which sort to the front and are skipped
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

    uint64_t temp;
    for (int i = 0; i < (XboxPartitionTableEntryCount - 1); i++)
    {
        if (starts[i] > starts[i + 1])
        {
            temp = starts[i];
            starts[i] = starts[i + 1];
            starts[i + 1] = temp;
            temp = ends[i];
            ends[i] = ends[i + 1];
            ends[i + 1] = temp;
            i = -1;
        }
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

    // First gap between partitions with room for at least one 4KB block once the
    // start is aligned. A gap of a few sectors would put the aligned start inside
    // the next partition.
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

    // After the last partition, if the disk is not full
    const uint64_t candidate = AlignStartTo4k(ends[(XboxPartitionTableEntryCount - 1)]);
    if (candidate + LBA_ALIGNMENT <= m_last_usable_lba)
    {
        return candidate;
    }

    // No free space
    return 0;
}

uint64_t XboxPartitionTableManipulator::GetAvailableFreeSpaceForPartition(const int partition_number) const
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return 0;
    }

    // A partition that was never placed must not grow from LBA 0
    if (GetPartitionStart(partition_number) == 0)
    {
        return 0;
    }

    if (GetActivePartitionCount() == 0)
    {
        return m_usable_lba_size;
    }

    uint64_t starts[XboxPartitionTableEntryCount], ends[XboxPartitionTableEntryCount];
    SortPartitions(starts, ends);

    for (int i = 0; i < (XboxPartitionTableEntryCount - 1); i++)
    {
        if (!starts[i])
        {
            continue;
        }

        if (starts[i] == GetPartitionStart(partition_number))
        {
            if (ends[i] > starts[i + 1])
            {
                // TODO: Error
                return 0;
            }
            else
            {
                return AlignTo4k(starts[i + 1] - ends[i]);
            }
        }
    }

    if (ends[(XboxPartitionTableEntryCount - 1)] >= m_last_usable_lba)
    {
        return 0;
    }
    else
    {
        return AlignTo4k(m_last_usable_lba - ends[(XboxPartitionTableEntryCount - 1)]);
    }
}

void XboxPartitionTableManipulator::PartitionIncreaseSizeBy(const int partition_number, uint64_t delta)
{
    if (partition_number >= XboxPartitionTableEntryCount)
    {
        return;
    }

    uint64_t max = GetAvailableFreeSpaceForPartition(partition_number);

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

    // A new (never placed) partition starts at LBA 0. A disabled one keeps its
    // old range, but other partitions may have been moved into it since.
    // Either way, place it at the first free spot (empty, to be grown by the user).
    const uint64_t start = GetPartitionStart(partition_number);
    const uint64_t end = start + GetPartitionSize(partition_number);
    if (start == 0 || !IsRangeFree(start, end))
    {
        const uint64_t free_start = GetFirstFreeStart();
        if (free_start == 0)
        {
            // Disk is full, leave the slot disabled
            return;
        }
        SetPartitionStart(partition_number, free_start);
        SetPartitionSize(partition_number, 0);
    }

    // Extended partitions made here get the same style of name as the presets
    if (partition_number >= XBOX_STANDARD_PARTITION_COUNT && IsNameBlank(partition_number))
    {
        std::string name = "XBOX EXTRA ";
        name += (char)('F' + (partition_number - XBOX_STANDARD_PARTITION_COUNT));
        SetPartitionName(partition_number, name);
    }

    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_IN_USE;
}

// True if [start, end) is inside the usable area and no enabled partition uses any of it
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

// Sets the in use flag without any placement logic, for the fixed preset layouts
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

int XboxPartitionTableManipulator::GetLastAvailablePartitionSlot() const
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

    if (part_start <= XBOX_SWAPPART1_LBA_START)
    {
        return 0;
    }

    uint64_t starts[XboxPartitionTableEntryCount], ends[XboxPartitionTableEntryCount];
    SortPartitions(starts, ends);

    for (int i = XboxPartitionTableEntryCount - 1; i >= 0; i--)
    {
        if (!ends[i])
        {
            continue;
        }

        // A zero size partition is its own "previous partition" (it ends where
        // it starts): skip it, or it could never move back
        if (starts[i] == part_start && ends[i] == part_start)
        {
            continue;
        }

        if (ends[i] <= part_start)
        {
            // Round the gap down so moving back never overlaps the previous partition
            return AlignTo4k(part_start - ends[i]);
        }
    }

    // Nothing in front of this partition, it can move back to the first usable LBA
    return AlignTo4k(part_start - XBOX_SWAPPART1_LBA_START);
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

    uint64_t part_start = GetPartitionStart(partition_number);
    SetPartitionStart(partition_number, part_start - delta);
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

    // SetPartitionStart rounds up to 4K, which for an unaligned start (tables
    // from other tools) could go past the free space and into the next
    // partition. Round down in that case instead.
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

    // Nothing to shrink: a zero size partition moves instead
    if (part_size == 0)
    {
        PartitionMoveForward(partition_number, delta);
        return;
    }

    // Keep at least one 4K block. The start is aligned up and the size down, so
    // the end never goes past where it was.
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

    // Like forward: a zero size partition has no end to keep, it moves
    if (GetPartitionSize(partition_number) == 0)
    {
        PartitionMoveBack(partition_number, delta);
        return;
    }

    const uint64_t free_space = GetAvailableFreeSpaceForPartitionBefore(partition_number);
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

    // Wipe reserved section
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

void XboxPartitionTableManipulator::SetupPartitionsFOnly()
{
    SetupPartitionsStock();

    // Stock sized drive, no room for F
    if (m_last_usable_lba <= XBOX_PART6_LBA_START)
    {
        return;
    }

    // Only F, capped to the max partition size (16TB)
    SetPartitionStart(5, XBOX_PART6_LBA_START);
    // Check max partition size (both sides in LBAs)
    if ((m_last_usable_lba - GetPartitionStart(5)) > (uint64_t)LBASIZE_16TB)
    {
        SetPartitionSize(5, LBASIZE_16TB);
    }
    else
    {
        SetPartitionSize(5, (m_last_usable_lba - GetPartitionStart(5)));
    }

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

    // F and G split over the free space
    SetPartitionStart(5, XBOX_PART6_LBA_START);
    SetPartitionSize(5, (m_last_usable_lba - GetPartitionStart(5)) / 2);
    SetPartitionName(5, LBA48_Partition_Name_F);
    MarkEnabled(5);

    SetPartitionStart(6, GetPartitionStart(5) + GetPartitionSize(5));
    SetPartitionSize(6, m_last_usable_lba - GetPartitionStart(6));
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

uint64_t XboxPartitionTableManipulator::GetTotalHDDLBAs() const
{
    return m_hdd_lba_size;
}

uint64_t XboxPartitionTableManipulator::AlignStartTo4k(const uint64_t value) const
{
    const uint64_t mask = LBA_ALIGNMENT_MASK;
    uint64_t aligned = value & mask;

    // If we aligned it backwards, we need to move it foward to prevent overlap
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

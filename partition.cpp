#include "partition.hpp"
#include <cstring>

XboxPartitionTableManipulator::XboxPartitionTableManipulator(const uint64_t total_hdd_lba, const bool lbav4) : m_hdd_lba_size(total_hdd_lba), m_lba_v4(lbav4)
{
    SetupMaxSize();
    SetupPartitionsStock();
}

XboxPartitionTableManipulator::XboxPartitionTableManipulator(const XboxPartitionTable &part_table, const uint64_t total_hdd_lba, const bool lbav4) : m_hdd_lba_size(total_hdd_lba), m_lba_v4(lbav4), m_table(part_table)
{

    SetupMaxSize();

    if (!HasMagic())
    {
        SetupPartitionsStock();
    }
}

void XboxPartitionTableManipulator::SetupMaxSize()
{
    m_usable_lba_size = m_hdd_lba_size - RESERVED_SPACE_END_LBA - XBOX_SWAPPART1_LBA_START;
    m_last_usable_lba = m_hdd_lba_size - RESERVED_SPACE_END_LBA;

    // Cap sizes to lba48 v3 and earlier sizes
    if (!m_lba_v4 && (m_hdd_lba_size > (uint64_t)MAX_HDD_SIZE_LBA48_V3))
    {
        m_usable_lba_size = MAX_HDD_SIZE_LBA48_V3 - RESERVED_SPACE_END_LBA - XBOX_SWAPPART1_LBA_START;
        m_last_usable_lba = MAX_HDD_SIZE_LBA48_V3 - RESERVED_SPACE_END_LBA;
    }
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
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        starts[i] = GetPartitionStart(i);
        ends[i] = GetPartitionStart(i) + GetPartitionSize(i);
    }

    // Sort
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

    for (int i = 0; i < (XboxPartitionTableEntryCount - 1); i++)
    {
        if (!starts[i])
        {
            continue;
        }

        if (ends[i] < starts[i + 1])
        {
            return AlignStartTo4k(ends[i]);
        }
    }

    return AlignStartTo4k(ends[(XboxPartitionTableEntryCount - 1)]);
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

    // A new (never placed) partition starts at LBA 0, move it to the first free spot
    if (GetPartitionStart(partition_number) == 0)
    {
        SetPartitionStart(partition_number, GetFirstFreeStart());
    }

    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_IN_USE;
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

    uint64_t max = GetAvailableFreeSpaceForPartition(partition_number);

    if (delta > max)
    {
        delta = max;
    }

    uint64_t part_start = GetPartitionStart(partition_number);
    SetPartitionStart(partition_number, part_start + delta);
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
    EnablePartition(0);
    SetPartitionStart(1, XBOX_SYSPART_LBA_START);
    SetPartitionSize(1, XBOX_SYSPART_LBA_SIZE);
    SetPartitionName(1, LBA48_Partition_Name_C);
    EnablePartition(1);
    SetPartitionStart(2, XBOX_SWAPPART1_LBA_START);
    SetPartitionSize(2, XBOX_SWAPPART_LBA_SIZE);
    SetPartitionName(2, LBA48_Partition_Name_X);
    EnablePartition(2);
    SetPartitionStart(3, XBOX_SWAPPART2_LBA_START);
    SetPartitionSize(3, XBOX_SWAPPART_LBA_SIZE);
    SetPartitionName(3, LBA48_Partition_Name_Y);
    EnablePartition(3);
    SetPartitionStart(4, XBOX_SWAPPART3_LBA_START);
    SetPartitionSize(4, XBOX_SWAPPART_LBA_SIZE);
    SetPartitionName(4, LBA48_Partition_Name_Z);
    EnablePartition(4);
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
    EnablePartition(5);
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
    EnablePartition(5);

    SetPartitionStart(6, GetPartitionStart(5) + GetPartitionSize(5));
    SetPartitionSize(6, m_last_usable_lba - GetPartitionStart(6));
    SetPartitionName(6, LBA48_Partition_Name_G);
    EnablePartition(6);
}

XboxPartitionTable XboxPartitionTableManipulator::GetPartitionTable()
{
    return m_table;
}

void XboxPartitionTableManipulator::LoadNewTable(const XboxPartitionTable &part_table)
{
    m_table = part_table;
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

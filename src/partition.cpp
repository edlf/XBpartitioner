#include "partition.hpp"
#include <string.h>

PartitionTableEditor::PartitionTableEditor(const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k)
    : m_hdd_lba_size(total_hdd_lba), m_lba_v4(lbav4), m_physical_4k(physical_4k)
{
    SetupMaxSize();
    SetupPartitionsStock();
}

PartitionTableEditor::PartitionTableEditor(const XboxPartitionTable &part_table, const uint64_t total_hdd_lba, const bool lbav4,
                                                             const bool physical_4k)
    : m_table(part_table), m_hdd_lba_size(total_hdd_lba), m_lba_v4(lbav4), m_physical_4k(physical_4k)
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
void PartitionTableEditor::DropHighLbaBitsIfNotV4()
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

bool PartitionTableEditor::HasUnknownVersion(const XboxPartitionTable &part_table)
{
    return HasMagic(part_table) && part_table.Version != XBOX_TABLE_VERSION_LBA48_V3 && part_table.Version != XBOX_TABLE_VERSION_LBA48_V4 &&
           part_table.Version != XBOX_TABLE_VERSION_TITAN;
}

unsigned char PartitionTableEditor::VersionFor(const bool lbav4)
{
    return lbav4 ? XBOX_TABLE_VERSION_LBA48_V4 : XBOX_TABLE_VERSION_LBA48_V3;
}

bool PartitionTableEditor::IsTableUsable(const XboxPartitionTable &part_table, const uint64_t total_sectors)
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

bool PartitionTableEditor::FindConflict(int *first, int *second) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!IsPartitionEnabled(i))
        {
            continue;
        }
        const uint64_t start = GetPartitionStart(i);
        const uint64_t end = start + GetPartitionSize(i);

        for (int j = i + 1; j < XboxPartitionTableEntryCount; j++)
        {
            if (!IsPartitionEnabled(j))
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

bool PartitionTableEditor::FindPartitionBeforeUsableStart(int *partition) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (IsPartitionEnabled(i) && GetPartitionStart(i) < XBOX_SWAPPART1_LBA_START)
        {
            *partition = i;
            return true;
        }
    }

    return false;
}

bool PartitionTableEditor::HasHighLbaBits(const XboxPartitionTable &part_table)
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

void PartitionTableEditor::SetupMaxSize()
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

uint64_t PartitionTableEditor::GetBackupTableLba() const
{
    return m_backup_table_lba;
}

uint64_t PartitionTableEditor::GetLastSectorLba() const
{
    return m_last_sector_lba;
}

bool PartitionTableEditor::OverlapsBackupTable() const
{
    return m_backup_table_lba != 0 && OverlapsSector(m_backup_table_lba);
}

bool PartitionTableEditor::OverlapsSector(const uint64_t lba) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (IsPartitionEnabled(i) && GetPartitionStart(i) <= lba && lba < GetPartitionStart(i) + GetPartitionSize(i))
        {
            return true;
        }
    }

    return false;
}

int PartitionTableEditor::FindBackupShrinkCandidate() const
{
    if (!OverlapsBackupTable())
    {
        return -1;
    }

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!IsPartitionEnabled(i))
        {
            continue;
        }
        const uint64_t start = GetPartitionStart(i);
        const uint64_t size = GetPartitionSize(i);
        if (start > m_backup_table_lba || m_backup_table_lba >= start + size)
        {
            continue;
        }

        // Only the first match matters; an overlap leaves nothing sensible to shrink
        if (size <= LBA_ALIGNMENT || !IsAlignedTo4k(size) || start + size - LBA_ALIGNMENT > m_backup_table_lba)
        {
            return -1;
        }
        return i;
    }

    return -1;
}

FatxLayout FatxLayoutFor(const uint64_t lba_size, const uint32_t cluster_bytes)
{
    FatxLayout layout;
    memset(&layout, 0, sizeof(layout));
    if (cluster_bytes == 0)
    {
        return layout;
    }

    const uint64_t length = lba_size * HDD_SECTOR_SIZE;

    // One entry per cluster the partition could hold, plus the reserved entry 0
    const uint64_t entries = length / cluster_bytes + 1;
    layout.bytes_per_entry = (entries < 0xFFF0) ? 2 : 4;
    layout.bytes_per_fat = (entries * layout.bytes_per_entry + FATX_HEADER_BYTES - 1) & ~(uint64_t)(FATX_HEADER_BYTES - 1);
    layout.data_offset = FATX_HEADER_BYTES + layout.bytes_per_fat;
    layout.cluster_limit = (length > layout.data_offset) ? (length - layout.data_offset) / cluster_bytes + 1 : 0;
    return layout;
}

static bool is_power_of_two(const uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

bool FatxIsMemoryUnit(const uint32_t sectors_per_cluster, const FatxMedium medium)
{
    if (medium != FATX_MEDIUM_UNKNOWN)
    {
        return medium == FATX_MEDIUM_MEMORY_UNIT;
    }
    return sectors_per_cluster < FATX_HDD_MIN_SECTORS_PER_CLUSTER;
}

uint32_t FatxClusterBytes(const uint32_t sectors_per_cluster, const FatxMedium medium)
{
    // Hard drives: 16 KB to 1 MB; memory units: 4 KB to 64 KB
    const bool memory_unit = FatxIsMemoryUnit(sectors_per_cluster, medium);
    const uint32_t least = memory_unit ? 1 : FATX_HDD_MIN_SECTORS_PER_CLUSTER;
    const uint32_t most = memory_unit ? FATX_HDD_MIN_SECTORS_PER_CLUSTER - 1 : 2048;
    if (!is_power_of_two(sectors_per_cluster) || sectors_per_cluster < least || sectors_per_cluster > most)
    {
        return 0;
    }
    return sectors_per_cluster * (memory_unit ? FATX_MEMORY_UNIT_SECTOR_SIZE : HDD_SECTOR_SIZE);
}

uint32_t FatxSectorsPerCluster(const uint32_t cluster_bytes, const bool memory_unit)
{
    const uint32_t sector = memory_unit ? FATX_MEMORY_UNIT_SECTOR_SIZE : HDD_SECTOR_SIZE;
    if (!is_power_of_two(cluster_bytes) || cluster_bytes % sector != 0)
    {
        return 0;
    }
    const uint32_t sectors = cluster_bytes / sector;
    const bool fits = memory_unit ? (sectors < FATX_HDD_MIN_SECTORS_PER_CLUSTER) : (sectors >= FATX_HDD_MIN_SECTORS_PER_CLUSTER && sectors <= 2048);
    return fits ? sectors : 0;
}

uint64_t FatxClusterAt(const FatxLayout &layout, const uint32_t cluster_bytes, const uint64_t byte_offset)
{
    if (cluster_bytes == 0 || byte_offset < layout.data_offset)
    {
        return 0;
    }

    const uint64_t cluster = (byte_offset - layout.data_offset) / cluster_bytes + 1;
    return (cluster < layout.cluster_limit) ? cluster : 0;
}

bool PartitionTableEditor::IsIndex(const int partition_number)
{
    return partition_number >= 0 && partition_number < (int)XboxPartitionTableEntryCount;
}

void PartitionTableEditor::SetPartitionName(const uint16_t partition_number, const char *name)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::SetPartitionSize(const int partition_number, const uint64_t value)
{
    if (!IsIndex(partition_number))
    {
        return;
    }

    const uint64_t aligned_lba = AlignTo4k(value);
    m_table.TableEntries[partition_number].LBASize = (uint32_t)(aligned_lba & LBA_LOWER_BITS);
    m_table.TableEntries[partition_number].LBASize_high = (uint16_t)(aligned_lba >> 32);
}

void PartitionTableEditor::SetPartitionStart(const int partition_number, const uint64_t value)
{
    if (!IsIndex(partition_number))
    {
        return;
    }

    const uint64_t lba_aligned = AlignStartTo4k(value);
    m_table.TableEntries[partition_number].LBAStart = (uint32_t)(lba_aligned & LBA_LOWER_BITS);
    m_table.TableEntries[partition_number].LBAStart_high = (uint16_t)(lba_aligned >> 32);
}

uint64_t PartitionTableEditor::GetPartitionSize(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        return 0;
    }

    return (uint64_t)m_table.TableEntries[partition_number].LBASize_high << 32 | m_table.TableEntries[partition_number].LBASize;
}

uint64_t PartitionTableEditor::GetPartitionStart(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        return 0;
    }

    return (uint64_t)m_table.TableEntries[partition_number].LBAStart_high << 32 | m_table.TableEntries[partition_number].LBAStart;
}

unsigned int PartitionTableEditor::CalculateClusterSizeForPartition(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        return 0;
    }

    return CalculateClusterSize(GetPartitionSize(partition_number));
}

unsigned int PartitionTableEditor::CalculateClusterSize(const uint64_t lba_size) const
{
    return ClusterSizeKbFor(lba_size);
}

unsigned int ClusterSizeKbFor(const uint64_t lba_size)
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

namespace
{

bool same_fatx_layout(const FatxLayout &layout, const uint64_t lba_size, const uint32_t cluster_bytes)
{
    const FatxLayout other = FatxLayoutFor(lba_size, cluster_bytes);
    return other.bytes_per_entry == layout.bytes_per_entry && other.bytes_per_fat == layout.bytes_per_fat && other.cluster_limit >= 2;
}

// The layout only grows with the size, so these search outwards from a size
// in doubling steps, then narrow down

uint64_t largest_same_layout(const FatxLayout &layout, const uint64_t from, const uint32_t cluster_bytes)
{
    uint64_t largest = from;
    uint64_t step = LBA_ALIGNMENT;
    while (step < LBA48_MAX_LBA && same_fatx_layout(layout, largest + step, cluster_bytes))
    {
        largest += step;
        step *= 2;
    }
    while (step > LBA_ALIGNMENT)
    {
        step /= 2;
        if (same_fatx_layout(layout, largest + step, cluster_bytes))
        {
            largest += step;
        }
    }
    return largest;
}

uint64_t smallest_same_layout(const FatxLayout &layout, const uint64_t from, const uint32_t cluster_bytes)
{
    uint64_t smallest = from;
    uint64_t step = LBA_ALIGNMENT;
    while (smallest > step && same_fatx_layout(layout, smallest - step, cluster_bytes))
    {
        smallest -= step;
        step *= 2;
    }
    while (step > LBA_ALIGNMENT)
    {
        step /= 2;
        if (smallest > step && same_fatx_layout(layout, smallest - step, cluster_bytes))
        {
            smallest -= step;
        }
    }
    return smallest;
}

// XBpartitioner's largest size for the cluster size; lba_size itself if the
// partition is already past it
uint64_t largest_size_for_clusters(const uint64_t lba_size, const uint32_t cluster_bytes)
{
    const unsigned int cluster_kb = cluster_bytes / 1024;
    if (ClusterSizeKbFor(lba_size) > cluster_kb)
    {
        return lba_size;
    }
    uint64_t largest = 0x20000000;
    while (ClusterSizeKbFor(largest * 2) <= cluster_kb && largest < LBA48_MAX_LBA)
    {
        largest *= 2;
    }
    return largest;
}

} // namespace

FatxLbaRange FatxSameLayoutRange(const uint64_t lba_size, const uint32_t cluster_bytes)
{
    const FatxLayout layout = FatxLayoutFor(lba_size, cluster_bytes);
    FatxLbaRange range;
    range.lowest = smallest_same_layout(layout, lba_size, cluster_bytes);
    range.highest = largest_same_layout(layout, lba_size, cluster_bytes);
    return range;
}

void FatxInPlaceResizeRange(const uint64_t lba_size, const uint32_t cluster_bytes, uint64_t *min_lba, uint64_t *max_lba)
{
    *min_lba = lba_size;
    *max_lba = lba_size;
    const FatxLayout layout = FatxLayoutFor(lba_size, cluster_bytes);
    const uint64_t aligned = lba_size & ~(uint64_t)(LBA_ALIGNMENT - 1);
    if (cluster_bytes == 0 || aligned == 0 || !same_fatx_layout(layout, aligned, cluster_bytes))
    {
        return;
    }

    // From the aligned size, which has the same FAT
    const FatxLbaRange same = FatxSameLayoutRange(aligned, cluster_bytes);
    const uint64_t smallest = same.lowest;
    uint64_t largest = same.highest;
    const uint64_t rule_max = largest_size_for_clusters(lba_size, cluster_bytes);
    if (largest > rule_max)
    {
        largest = (rule_max > lba_size) ? (rule_max & ~(uint64_t)(LBA_ALIGNMENT - 1)) : lba_size;
    }

    *min_lba = (smallest < lba_size) ? smallest : lba_size;
    *max_lba = (largest > lba_size) ? largest : lba_size;
}

void PartitionTableEditor::SortPartitions(uint64_t *starts, uint64_t *ends) const
{
    // Disabled entries take no space: 0, so they sort first and are skipped
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (IsPartitionEnabled(i))
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

int PartitionTableEditor::GetActivePartitionCount() const
{
    int count = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (IsPartitionEnabled(i))
        {
            count++;
        }
    }

    return count;
}

uint64_t PartitionTableEditor::GetFirstFreeStart() const
{
    uint64_t starts[XboxPartitionTableEntryCount], ends[XboxPartitionTableEntryCount];

    if (GetActivePartitionCount() == 0)
    {
        return XBOX_SWAPPART1_LBA_START;
    }

    SortPartitions(starts, ends);

    // First gap with room for a 4 KB block once the start is aligned. A gap
    // starts after the furthest end so far, so a partition inside an
    // overlapping one never seems to leave room.
    uint64_t covered_end = 0;
    for (int i = 0; i < (XboxPartitionTableEntryCount - 1); i++)
    {
        if (!starts[i])
        {
            continue;
        }

        covered_end = (ends[i] > covered_end) ? ends[i] : covered_end;
        const uint64_t candidate = AlignStartTo4k(covered_end);
        if (candidate + LBA_ALIGNMENT <= starts[i + 1])
        {
            return candidate;
        }
    }

    const uint64_t last_end = ends[XboxPartitionTableEntryCount - 1];
    const uint64_t candidate = AlignStartTo4k((last_end > covered_end) ? last_end : covered_end);
    if (candidate + LBA_ALIGNMENT <= m_last_usable_lba)
    {
        return candidate;
    }

    return 0;
}

uint64_t PartitionTableEditor::GetAvailableFreeSpaceForPartition(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        return 0;
    }

    // Disabled: no space to grow into. Never placed (0): must not grow from LBA 0.
    const uint64_t part_start = GetPartitionStart(partition_number);
    if (!IsPartitionEnabled(partition_number) || part_start == 0)
    {
        return 0;
    }

    // Compared by index, so one sharing this start is never mistaken for it
    const uint64_t part_end = part_start + GetPartitionSize(partition_number);
    uint64_t next_start = m_last_usable_lba;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (i == partition_number || !IsPartitionEnabled(i))
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

uint64_t PartitionTableEditor::GetMaxPartitionSize() const
{
    return m_lba_v4 ? ~(uint64_t)0 : (uint64_t)LBASIZE_LBA48_V3_MAX;
}

unsigned int PartitionTableEditor::GetMaxClusterSize() const
{
    return m_lba_v4 ? 0 : LBA48_V3_MAX_CLUSTER_KB;
}

// 0 when at or over it (a v4 table on v3 can only shrink)
static uint64_t room_below_max(uint64_t size, uint64_t max_size)
{
    return (size < max_size) ? max_size - size : 0;
}

uint64_t PartitionTableEditor::GetGrowthLimit(const int partition_number) const
{
    const uint64_t free_after = GetAvailableFreeSpaceForPartition(partition_number);
    const uint64_t room = room_below_max(GetPartitionSize(partition_number), GetMaxPartitionSize());
    return (free_after < room) ? free_after : room;
}

uint64_t PartitionTableEditor::GetGrowthLimitBefore(const int partition_number) const
{
    const uint64_t free_before = GetAvailableFreeSpaceForPartitionBefore(partition_number);
    // Both 4K aligned, so moving the start back by this keeps it aligned
    const uint64_t room = AlignTo4k(room_below_max(GetPartitionSize(partition_number), GetMaxPartitionSize()));
    return (free_before < room) ? free_before : room;
}

void PartitionTableEditor::PartitionIncreaseSizeBy(const int partition_number, uint64_t delta)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::PartitionDecreaseSizeBy(const int partition_number, uint64_t delta)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::EnablePartition(const int partition_number)
{
    if (!IsIndex(partition_number))
    {
        return;
    }

    if (IsPartitionEnabled(partition_number))
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
bool PartitionTableEditor::IsRangeFree(const uint64_t start, const uint64_t end) const
{
    if (start < XBOX_SWAPPART1_LBA_START || end > m_last_usable_lba || end < start)
    {
        return false;
    }

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (!IsPartitionEnabled(i))
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
void PartitionTableEditor::MarkEnabled(const int partition_number)
{
    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_IN_USE;
}

bool PartitionTableEditor::IsNameBlank(const int partition_number) const
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

void PartitionTableEditor::DisablePartition(const int partition_number)
{
    if (!IsIndex(partition_number))
    {
        return;
    }

    m_table.TableEntries[partition_number].Flags = PE_PARTFLAGS_EMPTY;
}

bool PartitionTableEditor::IsPartitionEnabled(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        return false;
    }

    return (m_table.TableEntries[partition_number].Flags == PE_PARTFLAGS_IN_USE);
}

void PartitionTableEditor::ErasePartition(const int partition_number)
{
    if (!IsIndex(partition_number))
    {
        return;
    }

    SetPartitionName(partition_number, "");
    SetPartitionStart(partition_number, 0);
    SetPartitionSize(partition_number, 0);
    m_table.TableEntries[partition_number].Flags = 0x0;
}

int PartitionTableEditor::GetFirstAvailablePartitionSlot() const
{
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (!IsPartitionEnabled(i))
        {
            return i;
        }
    }

    return -1;
}

uint64_t PartitionTableEditor::GetAvailableFreeSpaceForPartitionBefore(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        return 0;
    }

    const uint64_t part_start = GetPartitionStart(partition_number);

    if (!IsPartitionEnabled(partition_number) || part_start <= XBOX_SWAPPART1_LBA_START)
    {
        return 0;
    }

    // The previous partition is the latest end among those starting before
    uint64_t previous_end = XBOX_SWAPPART1_LBA_START;
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (i == partition_number || !IsPartitionEnabled(i))
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

void PartitionTableEditor::PartitionMoveBack(const int partition_number, uint64_t delta)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::PartitionMoveForward(const int partition_number, uint64_t delta)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::PartitionStartForward(const int partition_number, const uint64_t delta)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::PartitionStartBack(const int partition_number, uint64_t delta)
{
    if (!IsIndex(partition_number))
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

void PartitionTableEditor::MovePartitionStartTo(const int partition_number, const uint64_t start)
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

void PartitionTableEditor::ResizePartitionTo(const int partition_number, const uint64_t size)
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

uint64_t PartitionTableEditor::GetAvailableLBAs() const
{
    uint64_t used = 0;

    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (IsPartitionEnabled(i))
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

void PartitionTableEditor::SetupPartitionsStock()
{
    WriteMagic();

    m_table.Version = VersionFor(m_lba_v4);
    for (int i = 0; i < XboxPartitionTableHeaderReservedSize; i++)
    {
        m_table.Reserved[i] = 0;
    }

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

uint64_t PartitionTableEditor::PresetMaxPartitionSize() const
{
    const uint64_t max = GetMaxPartitionSize();
    return (max < (uint64_t)LBASIZE_16TB) ? max : (uint64_t)LBASIZE_16TB;
}

void PartitionTableEditor::SetupPartitionsFOnly()
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

void PartitionTableEditor::SetupPartitionsFGSplit()
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

XboxPartitionTable PartitionTableEditor::GetPartitionTable() const
{
    return m_table;
}

void PartitionTableEditor::LoadNewTable(const XboxPartitionTable &part_table)
{
    m_table = part_table;
    DropHighLbaBitsIfNotV4();
}

bool PartitionTableEditor::HasMagic(const XboxPartitionTable &part_table)
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

bool PartitionTableEditor::HasMagic() const
{
    return HasMagic(m_table);
}

void PartitionTableEditor::WriteMagic()
{
    for (size_t i = 0; i < XboxPartitionTableMagicSize; i++)
    {
        m_table.Magic[i] = LBA48_Partition_Table_Magic[i];
    }
}

XboxPartitionTableEntry PartitionTableEditor::GetPartitionEntry(const int partition_number) const
{
    if (!IsIndex(partition_number))
    {
        XboxPartitionTableEntry dummy = {0};
        return dummy;
    }

    return m_table.TableEntries[partition_number];
}

bool PartitionTableEditor::HasSameLayout(const PartitionTableEditor &other) const
{
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        const bool enabled = IsPartitionEnabled(i);
        if (enabled != other.IsPartitionEnabled(i))
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

bool PartitionTableEditor::RestorePartitionEntry(const int partition_number, const XboxPartitionTableEntry &entry)
{
    if (!IsIndex(partition_number))
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

uint64_t PartitionTableEditor::GetUsableLBAs() const
{
    return m_usable_lba_size;
}

uint64_t PartitionTableEditor::AlignStartTo4k(const uint64_t value) const
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

uint64_t PartitionTableEditor::AlignTo4k(const uint64_t value) const
{
    const uint64_t mask = LBA_ALIGNMENT_MASK;
    return value & mask;
}

bool PartitionTableEditor::IsAlignedTo4k(const uint64_t value) const
{
    return value == AlignTo4k(value);
}

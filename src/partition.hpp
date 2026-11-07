#ifndef PARTITION_H_
#define PARTITION_H_

#include <stddef.h>

typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned __int64 uint64_t;

#define IOCTL_CMD_LBA48_ACCESS 0xcafebabe
#define IOCTL_SUBCMD_GET_INFO 0

#define LBA48_GET_INFO_MAGIC1_IDX 0
#define LBA48_GET_INFO_MAGIC1_VAL 0xcafebabe
#define LBA48_GET_INFO_MAGIC2_IDX 1
#define LBA48_GET_INFO_MAGIC2_VAL 0xbabeface
#define LBA48_GET_INFO_PATCHCODE_VERSION_IDX 2
#define LBA48_GET_INFO_LOWCODE_BASE_IDX  3
#define LBA48_GET_INFO_HIGHCODE_BASE_IDX 4
#define LBA48_GET_INFO_PATCHSEG_SIZE_IDX 5
#define LBA48_GET_INFO_PART_TABLE_OFS_IDX 6

#define LBA48_Partition_Table_Magic "****PARTINFO****"
#define LBA48_Partition_Name_C "XBOX SHELL C"
#define LBA48_Partition_Name_E "XBOX DATA E"
#define LBA48_Partition_Name_X "XBOX CACHE X"
#define LBA48_Partition_Name_Y "XBOX CACHE Y"
#define LBA48_Partition_Name_Z "XBOX CACHE Z"
#define LBA48_Partition_Name_F "XBOX EXTRA F"
#define LBA48_Partition_Name_G "XBOX EXTRA G"

// The xbox currently only supports 512 byte hdd/ssds
const uint32_t HDD_SECTOR_SIZE = 512;

const uint32_t XBOX_SWAPPART1_LBA_START    = 0x400;
const uint32_t XBOX_SWAPPART_LBA_SIZE = 0x177000;
const uint32_t XBOX_SWAPPART2_LBA_START    = (XBOX_SWAPPART1_LBA_START + XBOX_SWAPPART_LBA_SIZE);
const uint32_t XBOX_SWAPPART3_LBA_START    = (XBOX_SWAPPART2_LBA_START + XBOX_SWAPPART_LBA_SIZE);

const uint32_t XBOX_SYSPART_LBA_START = (XBOX_SWAPPART3_LBA_START + XBOX_SWAPPART_LBA_SIZE);
const uint32_t XBOX_SYSPART_LBA_SIZE = 0xfa000;

const uint32_t XBOX_DATAPART_LBA_START = (XBOX_SYSPART_LBA_START + XBOX_SYSPART_LBA_SIZE);
const uint32_t XBOX_DATAPART_LBA_SIZE = 0x9896b0;

// F Start default is 0x00EE8AB0
const uint32_t XBOX_PART6_LBA_START = (XBOX_DATAPART_LBA_START + XBOX_DATAPART_LBA_SIZE);
const uint32_t XBOX_STANDARD_MAX_LBA = (XBOX_DATAPART_LBA_START + XBOX_DATAPART_LBA_SIZE);

// Partitions 1-5 (E, C, X, Y, Z) are the stock ones, 6-14 are extended (F, G, ...)
const int XBOX_STANDARD_PARTITION_COUNT = 5;

const uint32_t PE_PARTFLAGS_EMPTY = 0x0;
const uint32_t PE_PARTFLAGS_IN_USE = 0x80000000;

#define LBA48_MAX_LBA 0xFFFFFFFFFFFF
// Align to 4k sector boundaries (4096/512=8)
#define LBA_ALIGNMENT 0x8
#define LBA_ALIGNMENT_MASK (LBA48_MAX_LBA - (LBA_ALIGNMENT - 1))

#define SECTORS_PER_CLUSTER 32

#define SCALE_MEGABYTE 0x100000
#define SCALE_GIGABYTE 0x40000000

#define LBA_LOWER_BITS   0xFFFFFFFF
// Largest partition the presets make
#define LBASIZE_16TB    0x800000000
// v3 kernels only mount FATX clusters up to 64 KB: partitions at most 1TB
#define LBA48_V3_MAX_CLUSTER_KB  64
#define LBASIZE_LBA48_V3_MAX     0x80000000

// The backup table is at the end of the disk: the last sector, or on 512e
// drives the first sector of the last 4K block (aligned, like Fatxplorer).
// Partitions end before it.
#define RESERVED_SPACE_END_LBA 0x1
#define MAX_HDD_SIZE_LBA48_V3  0xFFFFFFFF

const uint32_t XboxPartitionNameMaxLength = 0x10;

typedef struct
{
    unsigned char Name[XboxPartitionNameMaxLength];
    uint32_t Flags;
    uint32_t LBAStart;
    uint32_t LBASize;
    uint16_t LBAStart_high;
    uint16_t LBASize_high;
} XboxPartitionTableEntry;

const uint32_t XboxPartitionTableMagicSize = 0x10;
const uint32_t XboxPartitionTableHeaderVersionSize = 0x1;
const uint32_t XboxPartitionTableHeaderReservedSize = 0x1F;
const uint32_t XboxPartitionTableEntrySize = 0x20;
const uint32_t XboxPartitionTableEntryCount = 14;
const uint32_t XboxPartitionTableFooterSize = 0x10;

const uint32_t XboxPartitionTableSize = XboxPartitionTableMagicSize + XboxPartitionTableHeaderVersionSize + XboxPartitionTableHeaderReservedSize + (XboxPartitionTableEntryCount * XboxPartitionTableEntrySize) + XboxPartitionTableFooterSize;


static_assert(sizeof(XboxPartitionTableEntry) == XboxPartitionTableEntrySize, "XboxPartitionTableEntry size is wrong!");

// XboxPartitionTable::Version: how the LBAs are stored
const unsigned char XBOX_TABLE_VERSION_LBA48_V3 = 0x00; // 32 bit LBAs
const unsigned char XBOX_TABLE_VERSION_LBA48_V4 = 0x01; // 48 bit LBAs (the _high fields)
const unsigned char XBOX_TABLE_VERSION_TITAN = 0x03;    // Titan patched BIOSes (only use the backup table)

typedef struct
{
    unsigned char Magic[XboxPartitionTableMagicSize];
    unsigned char Version;
    unsigned char Reserved[XboxPartitionTableHeaderReservedSize];
    XboxPartitionTableEntry    TableEntries[XboxPartitionTableEntryCount];
    unsigned char Footer[XboxPartitionTableFooterSize];
} XboxPartitionTable;

// The table is read and written as exactly one 512 byte sector
static_assert(sizeof(XboxPartitionTable) == XboxPartitionTableSize, "XboxPartitionTable size is wrong!");
static_assert(XboxPartitionTableSize == HDD_SECTOR_SIZE, "XboxPartitionTable must fill one sector!");
// The version right after the magic, the entries where they always were
static_assert(offsetof(XboxPartitionTable, Version) == 0x10, "XboxPartitionTable::Version is misplaced!");
static_assert(offsetof(XboxPartitionTable, TableEntries) == 0x30, "XboxPartitionTable::TableEntries is misplaced!");

class XboxPartitionTableManipulator {

public:
    // physical_4k: 512e drive, see GetBackupTableLba()
    XboxPartitionTableManipulator(const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k);
    XboxPartitionTableManipulator(const XboxPartitionTable& part_table, const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k);

    // 0 if there is none (unknown size, or no room after the stock partitions)
    uint64_t GetBackupTableLba() const;
    // Capped for pre v4; where older builds put the backup
    uint64_t GetLastSectorLba() const;
    // Writing the backup would then overwrite that partition, so it's skipped
    // (tables made before the backup moved on 512e drives)
    bool OverlapsBackupTable() const;
    bool OverlapsSector(const uint64_t lba) const;

    void SetupPartitionsStock();
    void SetupPartitionsFOnly();
    void SetupPartitionsFGSplit();

    XboxPartitionTable GetPartitionTable();

    void LoadNewTable(const XboxPartitionTable& part_table);

    // Up to XboxPartitionNameMaxLength characters, space padded
    void SetPartitionName(const uint16_t partition_number, const char *name);
    void SetPartitionStart(const int partition_number, const uint64_t value);
    void SetPartitionSize(const int partition_number, const uint64_t value);

    uint64_t GetPartitionSize(const int partition_number) const;
    uint64_t GetPartitionStart(const int partition_number) const;

    void ErasePartition(const int partition_number);

    void EnablePartition(const int partition_number);
    void DisablePartition(const int partition_number);
    bool isPartitionEnabled(const int partition_number) const;

    unsigned int CalculateClusterSizeForPartition(const int partition_number) const;
    unsigned int CalculateClusterSize(const uint64_t lba_size) const;

    XboxPartitionTableEntry getPartitionEntry(const int partition_number) const;
    // Names and the unused fields of disabled entries are ignored
    bool HasSameLayout(const XboxPartitionTableManipulator& other) const;
    // False if other partitions have moved into its range since
    bool RestorePartitionEntry(const int partition_number, const XboxPartitionTableEntry& entry);

    bool HasMagic() const;
    static bool HasMagic(const XboxPartitionTable& part_table);
    // lba48 v4 only
    static bool HasHighLbaBits(const XboxPartitionTable& part_table);
    // Has the magic and a version other than v3, v4 or Titan
    static bool HasUnknownVersion(const XboxPartitionTable& part_table);
    // The version a table written in this mode gets
    static unsigned char VersionFor(const bool lbav4);
    // Magic, at least one partition in use, each with a size, after the first
    // usable sector, inside the disk (if total_sectors is known), no overlaps
    static bool IsTableUsable(const XboxPartitionTable& part_table, const uint64_t total_sectors);
    // Overlapping or sharing a start (also when one is empty): must not be written
    bool FindConflict(int *first, int *second) const;
    // Starts in the reserved area before XBOX_SWAPPART1_LBA_START (damaged or
    // foreign kernel tables): must not be written or formatted
    bool FindPartitionBeforeUsableStart(int *partition) const;
    void WriteMagic();

    int GetFirstAvailablePartitionSlot() const;
    int GetActivePartitionCount() const;
    uint64_t GetUsableLBAs() const;

    // 0 if the disk is full
    uint64_t GetFirstFreeStart() const;

    uint64_t GetAvailableLBAs() const;

    // Up to the next partition or the end of the disk
    uint64_t GetAvailableFreeSpaceForPartition(const int partition_number) const;

    // Back to the previous partition or the first usable sector
    uint64_t GetAvailableFreeSpaceForPartitionBefore(const int partition_number) const;

    // No limit with v4 (the presets stop at 16TB), 1TB with v3 (64 KB clusters)
    uint64_t GetMaxPartitionSize() const;
    // 0 for no limit
    unsigned int GetMaxClusterSize() const;
    // The free space after (or before) it, up to GetMaxPartitionSize()
    uint64_t GetGrowthLimit(const int partition_number) const;
    uint64_t GetGrowthLimitBefore(const int partition_number) const;

    void PartitionIncreaseSizeBy(const int partition_number, const uint64_t delta);
    void PartitionDecreaseSizeBy(const int partition_number, const uint64_t delta);

    // The end stays: forward shrinks (down to one 4K block), back grows. A zero
    // size partition moves instead.
    void PartitionStartForward(const int partition_number, const uint64_t delta);
    void PartitionStartBack(const int partition_number, const uint64_t delta);

    // Typed values: clamped and aligned like the steps above
    void MovePartitionStartTo(const int partition_number, const uint64_t start);
    void ResizePartitionTo(const int partition_number, const uint64_t size);

    uint64_t AlignStartTo4k(const uint64_t value) const;
    uint64_t AlignTo4k(const uint64_t value) const;
    bool isAlignedTo4k(const uint64_t value) const;

private:
    void SortPartitions(uint64_t *starts, uint64_t *ends) const;
    // The presets' size cap: 16TB, or GetMaxPartitionSize() if smaller
    uint64_t PresetMaxPartitionSize() const;

    // For zero size partitions: the whole partition moves
    void PartitionMoveBack(const int partition_number, const uint64_t delta);
    void PartitionMoveForward(const int partition_number, const uint64_t delta);

    void SetupMaxSize();
    void DropHighLbaBitsIfNotV4();
    bool IsRangeFree(const uint64_t start, const uint64_t end) const;
    bool IsNameBlank(const int partition_number) const;
    void MarkEnabled(const int partition_number);

    XboxPartitionTable m_table;
    uint64_t m_hdd_lba_size;
    // Without the space before the first usable sector and the backup
    uint64_t m_usable_lba_size;
    // Last position where a partition can land
    uint64_t m_last_usable_lba;
    uint64_t m_backup_table_lba;
    uint64_t m_last_sector_lba;
    bool m_lba_v4;
    bool m_physical_4k;
};

// "FATX"
#define FATX_signature 1481916742

// Code stolen from PrometheOS
#define FAT_VOLUME_NAME_LENGTH          32
#define FAT_ONLINE_DATA_LENGTH          2048

typedef struct _FAT_VOLUME_METADATA {
    uint32_t Signature;
    uint32_t SerialNumber;
    uint32_t SectorsPerCluster;
    uint32_t RootDirFirstCluster;
    wchar_t VolumeName[FAT_VOLUME_NAME_LENGTH];
    unsigned char OnlineData[FAT_ONLINE_DATA_LENGTH];
    // Unused space in the block is filled with 0xFF bytes.
} FAT_VOLUME_METADATA, * PFAT_VOLUME_METADATA;

// On disk layout: the fix-up writes SectorsPerCluster in place
static_assert(offsetof(FAT_VOLUME_METADATA, Signature) == 0, "FAT_VOLUME_METADATA layout is wrong!");
static_assert(offsetof(FAT_VOLUME_METADATA, SectorsPerCluster) == 8, "FAT_VOLUME_METADATA layout is wrong!");
static_assert(offsetof(FAT_VOLUME_METADATA, VolumeName) == 16, "FAT_VOLUME_METADATA layout is wrong!");
static_assert(sizeof(FAT_VOLUME_METADATA) == 0x850, "FAT_VOLUME_METADATA layout is wrong!");
static_assert(sizeof(FAT_VOLUME_METADATA) <= HDD_SECTOR_SIZE * 8, "FAT_VOLUME_METADATA must fit the 4 KB read buffer!");

#define ROUND_TO_PAGES(Size)  (((ULONG_PTR)(Size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
// Theft over

#endif // PARTITION_H_
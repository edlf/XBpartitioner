#ifndef PARTITION_H_
#define PARTITION_H_

#include <string>

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

const uint32_t PE_PARTFLAGS_EMPTY = 0x0;
const uint32_t PE_PARTFLAGS_IN_USE = 0x80000000;

#define LBA48_MAX_LBA 0xFFFFFFFFFFFF
// Align to 4k sector boundaries (4096/512=8)
#define LBA_ALIGNMENT 0x8
// Clears the low 3 bits (0xFFFFFFFFFFF8)
#define LBA_ALIGNMENT_MASK (LBA48_MAX_LBA - (LBA_ALIGNMENT - 1))

#define CLUSTER_SIZE 0x4000
#define SECTORS_PER_CLUSTER 32

#define SCALE_MEGABYTE 0x100000
#define SCALE_GIGABYTE 0x40000000

#define LBA_LOWER_BITS   0xFFFFFFFF
// Sizes that require LBA48:
#define LBASIZE_16TB    0x800000000 // Max hdd size
#define LBASIZE_8TB     0x400000000
#define LBASIZE_4TB     0x200000000
#define LBASIZE_2TB     0x100000000
// Sizes that only require LBA32:
#define LBASIZE_1TB     0x80000000
#define LBASIZE_512GB   0x40000000
#define LBASIZE_256GB   0x20000000

// The backup partition table is kept at the end of the disk:
//   512n drives (512 byte physical sectors): the last sector
//   512e drives (4K physical, 512 byte logical): the first 512 byte sector of
//   the last full 4K block, so the write is aligned (same place as Cerbios)
// Partitions end before it. See XboxPartitionTableManipulator::GetBackupTableLba().
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
const uint32_t XboxPartitionTableHeaderReservedSize = 0x20;
const uint32_t XboxPartitionTableEntrySize = 0x20;
const uint32_t XboxPartitionTableEntryCount = 14;
const uint32_t XboxPartitionTableFooterSize = 0x10;

const uint32_t XboxPartitionTableSize = XboxPartitionTableMagicSize + XboxPartitionTableHeaderReservedSize + (XboxPartitionTableEntryCount * XboxPartitionTableEntrySize) + XboxPartitionTableFooterSize;


// static_assert(sizeof(XboxPartitionTableEntry) ==  XboxPartitionTableEntrySize, "XboxPartitionTableEntry size is wrong!");

typedef struct
{
    unsigned char Magic[XboxPartitionTableMagicSize];
    unsigned char Reserved[XboxPartitionTableHeaderReservedSize];
    XboxPartitionTableEntry    TableEntries[XboxPartitionTableEntryCount];
    unsigned char Footer[XboxPartitionTableFooterSize];
} XboxPartitionTable;

// static_assert(sizeof(XboxPartitionTable) ==  XboxPartitionTableSize, "XboxPartitionTable size is wrong!");

class XboxPartitionTableManipulator {

public:
    // physical_4k: the drive has 4K physical sectors (512e), see GetBackupTableLba()
    XboxPartitionTableManipulator(const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k);
    XboxPartitionTableManipulator(const XboxPartitionTable& part_table, const uint64_t total_hdd_lba, const bool lbav4, const bool physical_4k);

    // Sector holding the backup partition table, 0 if the disk size is unknown.
    // Partitions always end before it.
    uint64_t GetBackupTableLba() const;
    // Last addressable sector (capped for pre v4 lba48), where older builds put the backup
    uint64_t GetLastSectorLba() const;
    // True if a partition in use reaches the backup table sector. Writing the
    // backup would then overwrite the end of that partition. Happens with tables
    // made before the backup moved to the last 4K block on 512e drives.
    bool OverlapsBackupTable() const;

    void SetupPartitionsStock();
    void SetupPartitionsFOnly();
    void SetupPartitionsFGSplit();

    XboxPartitionTable GetPartitionTable();

    // Replace loaded table
    void LoadNewTable(const XboxPartitionTable& part_table);

    // Individual Partition Methods
    void SetPartitionName(const uint16_t partition_number, const std::string& name);
    void SetPartitionStart(const int partition_number, const uint64_t value);
    void SetPartitionSize(const int partition_number, const uint64_t value);

    uint64_t GetPartitionSize(const int partition_number) const;
    uint64_t GetPartitionStart(const int partition_number) const;
    uint64_t GetPartitionSizeInBytes(const int partition_number) const;

    // Wipes partition entry and disables partition
    void ErasePartition(const int partition_number);

    // Sets or unsets enabled bit
    void EnablePartition(const int partition_number);
    void DisablePartition(const int partition_number);
    bool isPartitionEnabled(const int partition_number) const;

    // Calculates expected cluster size for partition
    unsigned int CalculateClusterSizeForPartition(const int partition_number) const;
    unsigned int CalculateClusterSize(const uint64_t lba_size) const;

    XboxPartitionTableEntry getPartitionEntry(const int partition_number) const;

    // Partition table methods
    bool HasMagic() const;
    static bool HasMagic(const XboxPartitionTable& part_table);
    // True if a partition in use has LBAs above 32 bits (lba48 v4 only)
    static bool HasHighLbaBits(const XboxPartitionTable& part_table);
    void WriteMagic();

    int GetLastAvailablePartitionSlot() const;
    int GetActivePartitionCount() const;
    uint64_t GetUsableLBAs() const;
    uint64_t GetTotalHDDLBAs() const;

    // Gets first free lba for a new partition, 0 if the disk is full
    uint64_t GetFirstFreeStart() const;

    // Gets available free space
    uint64_t GetAvailableLBAs() const;

    // Gets available free space in front of a partition
    uint64_t GetAvailableFreeSpaceForPartition(const int partition_number) const;

    // Gets available free space behind a partition
    uint64_t GetAvailableFreeSpaceForPartitionBefore(const int partition_number) const;

    // Increase/decrease partition sizes
    void PartitionIncreaseSizeBy(const int partition_number, const uint64_t delta);
    void PartitionDecreaseSizeBy(const int partition_number, const uint64_t delta);

    // Moves partitions
    void PartitionMoveBack(const int partition_number, const uint64_t delta);
    void PartitionMoveForward(const int partition_number, const uint64_t delta);

    uint64_t AlignStartTo4k(const uint64_t value) const;
    uint64_t AlignTo4k(const uint64_t value) const;
    bool isAlignedTo4k(const uint64_t value) const;

private:
    // Sorts partitions
    void SortPartitions(uint64_t *starts, uint64_t *ends) const;

    void SetupMaxSize();
    void DropHighLbaBitsIfNotV4();

    XboxPartitionTable m_table;
    uint64_t m_hdd_lba_size;
    // Includes padding at the start and end of the disk
    // Both for stock config data and lba48 partition table backup
    uint64_t m_usable_lba_size;
    // Last position where a partition can land
    uint64_t m_last_usable_lba;
    uint64_t m_backup_table_lba;
    uint64_t m_last_sector_lba;
    bool m_lba_v4;
    bool m_physical_4k;
};

typedef struct _FATX_SUPERBLOCK
{
    char    Tag[4];
    uint32_t VolumeID;
    uint32_t ClusterSize;
    uint16_t FatCopies;
    int Resvd;
    char Unused[4078];
} FATX_SUPERBLOCK;

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

#define ROUND_TO_PAGES(Size)  (((ULONG_PTR)(Size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
// Theft over

#endif // PARTITION_H_
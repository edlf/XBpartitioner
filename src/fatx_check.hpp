#ifndef FATX_CHECK_H_
#define FATX_CHECK_H_

// FATX file system check and repairs, shared by the Xbox app, fatxtool and
// the tests. No standard library (the XDK runtime is from 2003). The FAT can
// be hundreds of MB, so it's read in chunks; only a bitmap of used clusters
// is kept in memory.

#include "partition.hpp"

// Reads a partition. Offsets and lengths are in whole sectors.
class FatxReader
{
public:
    virtual ~FatxReader() {}
    virtual bool Read(uint64_t offset, void *buffer, uint32_t length) = 0;
};

// A writable partition, for repairs
class FatxDevice : public FatxReader
{
public:
    virtual bool Write(uint64_t offset, const void *buffer, uint32_t length) = 0;
    // Flushes writes to the disk, so later writes can't land first
    virtual bool Sync() { return true; }
};

// Receives 0 to 100 as the work progresses
class FatxProgress
{
public:
    virtual ~FatxProgress() {}
    virtual void Update(int percent) = 0;
};

// Fields of the 4 KB header
const uint32_t FATX_SIGNATURE_OFFSET = 0;
const uint32_t FATX_SECTORS_PER_CLUSTER_OFFSET = 8;
const uint32_t FATX_ROOT_CLUSTER_OFFSET = 12;

// FAT values, as 32 bits. 16 bit entries are sign extended, so 0xFFFF is
// also FATX_FAT_END.
const uint32_t FATX_FAT_FREE = 0;
const uint32_t FATX_FAT_RESERVED = 0xFFFFFFF0; // This and above are markers, not clusters
const uint32_t FATX_FAT_BAD = 0xFFFFFFF7;
const uint32_t FATX_FAT_MEDIA = 0xFFFFFFF8; // Entry 0
const uint32_t FATX_FAT_END = 0xFFFFFFFF;

const uint32_t FATX_MAX_NAME_LENGTH = 42;
const unsigned char FATX_DIRENT_DELETED = 0xE5;
// Either one ends a folder
const unsigned char FATX_DIRENT_END = 0xFF;
const unsigned char FATX_DIRENT_END2 = 0x00;
const unsigned char FATX_ATTR_DIRECTORY = 0x10;

#pragma pack(push, 1)
typedef struct
{
    unsigned char name_length;
    unsigned char attributes;
    char name[FATX_MAX_NAME_LENGTH];
    uint32_t first_cluster;
    uint32_t file_size;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_time;
    uint16_t accessed_date;
} FatxDirent;
#pragma pack(pop)

static_assert(sizeof(FatxDirent) == 64, "FatxDirent must be 64 bytes!");

const int FATX_CHECK_MAX_MESSAGES = 64;
const int FATX_CHECK_MESSAGE_LENGTH = 160;

typedef struct
{
    bool error; // Otherwise a warning
    char text[FATX_CHECK_MESSAGE_LENGTH];
} FatxCheckMessage;

enum FatxCheckStatus
{
    FATX_CHECK_DONE = 0,   // See errors and warnings
    FATX_CHECK_NOT_FATX,   // Unformatted, or another file system
    FATX_CHECK_RESIZING,   // An unfinished resize, see fatx_resize.hpp
    FATX_CHECK_BAD_HEADER, // Invalid cluster size or root
    FATX_CHECK_READ_ERROR,
    FATX_CHECK_NO_MEMORY
};

// About 10 KB, so allocate it rather than using the Xbox's 64 KB stack
typedef struct
{
    FatxCheckStatus status;
    uint32_t cluster_bytes;
    uint32_t root_cluster;
    FatxLayout layout;

    uint64_t files;
    uint64_t directories;
    uint64_t used_clusters; // Not free and not bad
    uint64_t bad_clusters;
    uint64_t lost_clusters; // Marked used, but no file uses them

    // Everything is counted, but only the first FATX_CHECK_MAX_MESSAGES are kept
    uint32_t errors;
    uint32_t warnings;
    uint32_t message_count;
    FatxCheckMessage messages[FATX_CHECK_MAX_MESSAGES];
} FatxCheckResult;

// Walks every folder and chain from the root and compares with the FAT.
// Errors are things the kernel would read wrong; warnings are harmless.
// Returns false if the check couldn't run.
bool FatxCheck(FatxReader &reader, uint64_t partition_bytes, FatxCheckResult *result, FatxProgress *progress = NULL);

enum FatxRepairStatus
{
    FATX_REPAIR_DONE = 0,      // Repaired, or would be in a dry run
    FATX_REPAIR_NOTHING_TO_DO,
    FATX_REPAIR_HAS_ERRORS,    // Nothing was touched
    FATX_REPAIR_NOT_CHECKED,   // See check.status; nothing was written
    FATX_REPAIR_STOPPED_PARTWAY // Failed after writing began; check again
};

typedef struct
{
    FatxRepairStatus status;
    bool written;                 // False for a dry run or when there was nothing to do
    uint32_t files_trimmed;       // Chains cut to their file's size
    uint32_t empty_files_cleared; // Empty files that still had clusters
    uint64_t clusters_freed;      // Lost clusters plus what the above released
    bool media_fixed;             // FAT entry 0 reset to 0xFFFFFFF8
    FatxCheckResult check;        // Before the repair
} FatxRepairResult;

// Repairs that don't change any file's contents: frees lost clusters, cuts
// over-long chains, frees empty files' clusters and resets FAT entry 0.
// Refuses if the check finds errors, since a "lost" cluster might then
// belong to a damaged file. With write false it only counts. The partition
// must not be mounted while it's written.
bool FatxRepair(FatxDevice &device, uint64_t partition_bytes, FatxRepairResult *repair, bool write,
                FatxProgress *progress = NULL);

enum FatxResizeStatus
{
    FATX_RESIZE_OK = 0,          // Only the table needs to change
    FATX_RESIZE_NO_FILE_SYSTEM,  // Not FATX, so nothing to keep
    FATX_RESIZE_OUT_OF_RANGE,    // The FAT would change size
    FATX_RESIZE_CLUSTERS_IN_USE, // Shrinking would cut off used clusters
    FATX_RESIZE_FAT_NOT_CLEAN,   // Growing: the new clusters' FAT entries aren't 0
    FATX_RESIZE_HAS_ERRORS,      // Shrinking needs a clean check
    FATX_RESIZE_NOT_CHECKED      // Read error, out of memory or bad header
};

typedef struct
{
    FatxResizeStatus status;
    uint32_t cluster_bytes;
    uint64_t min_lba; // See FatxInPlaceResizeRange()
    uint64_t max_lba;
    uint64_t clusters_in_use; // Past the new end, lost ones included
    FatxCheckResult check;    // Only filled in when shrinking
} FatxResizeResult;

// Whether resizing (same start) only needs a table change. Growing reads a
// few FAT sectors; shrinking runs a full check, since the clusters it drops
// must be unused. Read only.
bool FatxCheckResize(FatxReader &reader, uint64_t old_lba_size, uint64_t new_lba_size, FatxResizeResult *resize,
                     FatxProgress *progress = NULL);

#endif // FATX_CHECK_H_

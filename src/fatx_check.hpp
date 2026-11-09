#ifndef FATX_CHECK_H_
#define FATX_CHECK_H_

// FATX file system check and safe repairs, shared by the Xbox app, the PC
// tool (tools/fatxtool) and the tests. Plain C++ without the standard library:
// the XDK's runtime is from 2003, and the Xbox has 64 MB of RAM, so the FAT
// (up to hundreds of MB) is read in chunks and only a bitmap of the
// clusters in use is kept.

#include "partition.hpp"

// Reads from the start of one partition. Offsets and lengths are whole
// 512 byte sectors.
class FatxReader
{
public:
    virtual ~FatxReader() {}
    virtual bool Read(uint64_t offset, void *buffer, uint32_t length) = 0;
};

// Same, and writable, for repairs
class FatxDevice : public FatxReader
{
public:
    virtual bool Write(uint64_t offset, const void *buffer, uint32_t length) = 0;
};

// 0 to 100, called when it changes. A repair's check and its own pass share it.
class FatxProgress
{
public:
    virtual ~FatxProgress() {}
    virtual void Update(int percent) = 0;
};

// The 4 KB header at the start of the partition
const uint32_t FATX_SIGNATURE_OFFSET = 0;
const uint32_t FATX_SECTORS_PER_CLUSTER_OFFSET = 8;
const uint32_t FATX_ROOT_CLUSTER_OFFSET = 12;

// FAT entries as 32 bit values (16 bit ones are sign extended, so 0xFFFF is
// FATX_FAT_END too)
const uint32_t FATX_FAT_FREE = 0;
const uint32_t FATX_FAT_RESERVED = 0xFFFFFFF0; // From here on: markers, not clusters
const uint32_t FATX_FAT_BAD = 0xFFFFFFF7;
const uint32_t FATX_FAT_MEDIA = 0xFFFFFFF8; // Entry 0
const uint32_t FATX_FAT_END = 0xFFFFFFFF;

const uint32_t FATX_MAX_NAME_LENGTH = 42;
const unsigned char FATX_DIRENT_DELETED = 0xE5;
// Either one ends a directory
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
    bool error; // Else a warning
    char text[FATX_CHECK_MESSAGE_LENGTH];
} FatxCheckMessage;

enum FatxCheckStatus
{
    FATX_CHECK_DONE = 0,   // Checked: see errors and warnings
    FATX_CHECK_NOT_FATX,   // No FATX signature (unformatted, or something else)
    FATX_CHECK_BAD_HEADER, // FATX, but the cluster size or root can't be right
    FATX_CHECK_READ_ERROR,
    FATX_CHECK_NO_MEMORY
};

// About 10 KB: allocate it, the Xbox stack is 64 KB
typedef struct
{
    FatxCheckStatus status;
    uint32_t cluster_bytes;
    uint32_t root_cluster;
    FatxLayout layout;

    uint64_t files;
    uint64_t directories;
    uint64_t used_clusters; // Not free in the FAT (bad ones not included)
    uint64_t bad_clusters;
    uint64_t lost_clusters; // Used in the FAT, but by no file or directory

    // All of them are counted; the first FATX_CHECK_MAX_MESSAGES are kept
    uint32_t errors;
    uint32_t warnings;
    uint32_t message_count;
    FatxCheckMessage messages[FATX_CHECK_MAX_MESSAGES];
} FatxCheckResult;

// Walks every directory from the root, follows every chain, then compares
// with the FAT. Errors: things the kernel would read wrong (chains into free
// or out of range clusters, clusters used twice, sizes longer than their
// chain, broken directory entries). Warnings: harmless but not clean (lost
// clusters, chains longer than the size, names the kernel refuses).
// False unless the check ran (status FATX_CHECK_DONE).
bool FatxCheck(FatxReader &reader, uint64_t partition_bytes, FatxCheckResult *result, FatxProgress *progress = NULL);

enum FatxRepairStatus
{
    FATX_REPAIR_DONE = 0,      // Repaired (or, for a dry run, would be)
    FATX_REPAIR_NOTHING_TO_DO, // Nothing the safe repairs fix
    FATX_REPAIR_HAS_ERRORS,    // The check found errors: nothing touched
    FATX_REPAIR_NOT_CHECKED,   // See check.status; nothing was written
    FATX_REPAIR_STOPPED_PARTWAY // A read or write failed after writing began: check again
};

typedef struct
{
    FatxRepairStatus status;
    bool written;                 // False for a dry run, or with nothing to do
    uint32_t files_trimmed;       // Chains ended where the size says
    uint32_t empty_files_cleared; // Empty files that had clusters
    uint64_t clusters_freed;      // Lost ones, and what the two above let go
    bool media_fixed;             // FAT entry 0 set back to 0xFFFFFFF8
    FatxCheckResult check;        // From before the repair
} FatxRepairResult;

// The safe repairs, which change nothing a file reads as: free lost
// clusters, end chains that are longer than their file, free the clusters
// of empty files, set FAT entry 0 back. Only when the check finds no errors:
// with errors, "lost" or "extra" clusters may belong to a file whose entry is
// damaged, or to a cross-linked one, and freeing them would lose its data.
// write false: a dry run, the counts say what would change. The partition
// must not be in use (mounted) while it's written.
bool FatxRepair(FatxDevice &device, uint64_t partition_bytes, FatxRepairResult *repair, bool write,
                FatxProgress *progress = NULL);

#endif // FATX_CHECK_H_

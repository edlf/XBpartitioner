#ifndef FATX_RESIZE_H_
#define FATX_RESIZE_H_

// Resizes a FATX partition when its FAT changes size, which gives every
// cluster a new number. Used by the Xbox app and fatxtool.
//
// 1. Run the safe repairs, so nothing is lost or extra.
// 2. Move data out of the way: out of where the bigger FAT will go, or past
//    the new end. Each cluster is copied, relinked, then freed, so a power
//    cut can leave lost clusters but nothing worse.
// 3. Renumber the FAT and every folder. Each piece goes through a spare
//    cluster first, and a journal in the header's unused last sector tracks
//    progress. The signature reads "XRSZ" meanwhile so nothing mounts it.
// 4. Record the new size, then restore the signature.
//
// FatxResumeResize() finishes steps 3 and 4 after a power cut. Not handled:
// crossing between 2 and 4 byte FAT entries, growing past ClusterSizeKbFor(),
// too little free space, or a check with errors.

#include "fatx_check.hpp"

// "XRSZ", the signature while a resize is unfinished
const uint32_t FATX_RESIZING_SIGNATURE = 0x5A535258;

// Stores the partition's new size (partition table, image length). May be
// called again on resume, so it must be safe to repeat.
class FatxResizeTable
{
public:
    virtual ~FatxResizeTable() {}
    virtual bool SetSize(uint64_t new_lba_size) = 0;
};

enum FatxMoveStatus
{
    FATX_MOVE_DONE = 0,
    FATX_MOVE_PLANNED,         // Dry run; it can be done
    FATX_MOVE_SAME_FAT,        // FAT keeps its size; use FatxCheckResize()
    FATX_MOVE_NOT_FATX,
    FATX_MOVE_HAS_ERRORS,      // See repair.check
    FATX_MOVE_ENTRY_SIZE,      // FAT entries would change between 2 and 4 bytes
    FATX_MOVE_CLUSTER_SIZE,    // Too big for the cluster size
    FATX_MOVE_TOO_SMALL,
    FATX_MOVE_NO_ROOM,         // Not enough free clusters to move into
    FATX_MOVE_NOT_CHECKED,     // Read error or out of memory; nothing changed
    FATX_MOVE_STOPPED_SAFE,    // Stopped while moving; still the old size and valid
    FATX_MOVE_INTERRUPTED,     // Stopped while renumbering; needs FatxResumeResize()
    FATX_MOVE_NOTHING_TO_RESUME
};

typedef struct
{
    FatxMoveStatus status;
    uint32_t cluster_bytes;
    uint64_t old_lba_size;
    uint64_t new_lba_size;    // Rounded down so the FAT changes by whole clusters
    int64_t shift;            // How many clusters the FAT grows by (negative: shrinks)
    uint64_t clusters_to_move;
    uint64_t free_clusters;   // Room to move them into
    uint64_t clusters_moved;
    FatxRepairResult repair;  // Step 1's result
} FatxMoveResult;

// The largest size up to requested_lba where the FAT changes by whole
// clusters, or 0. May land on a size where the FAT doesn't change at all.
uint64_t FatxMoveResizeTarget(uint64_t old_lba_size, uint32_t cluster_bytes, uint64_t requested_lba);

// The smallest size above from_lba where the FAT has grown by whole
// clusters, or 0
uint64_t FatxMoveResizeNextUp(uint64_t old_lba_size, uint32_t cluster_bytes, uint64_t from_lba);

enum FatxResizeKind
{
    FATX_RESIZE_KIND_FORMAT,   // Needs a format
    FATX_RESIZE_KIND_IN_PLACE, // Table change only (FatxCheckResize)
    FATX_RESIZE_KIND_MOVES     // Clusters move (FatxResizeWithMoves)
};

// Judged from the sizes alone; free space and errors are checked when writing
FatxResizeKind FatxClassifyResize(uint64_t old_lba_size, uint32_t cluster_bytes, uint64_t new_lba_size);

// With write false it only checks. The partition must not be mounted.
bool FatxResizeWithMoves(FatxDevice &device, uint64_t old_lba_size, uint64_t requested_lba, FatxResizeTable &table,
                         bool write, FatxMoveResult *result, FatxProgress *progress = NULL);

// Finishes a resize interrupted in step 3 or 4
bool FatxResumeResize(FatxDevice &device, FatxResizeTable &table, FatxMoveResult *result,
                      FatxProgress *progress = NULL);

// The sizes of an unfinished resize, or false if there isn't one
bool FatxFindInterruptedResize(FatxReader &reader, uint64_t *old_lba_size, uint64_t *new_lba_size);

#endif // FATX_RESIZE_H_

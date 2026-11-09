#ifndef FATX_RECLUSTER_H_
#define FATX_RECLUSTER_H_

// Changes a FATX partition's cluster size, keeping every file. Every byte
// moves and the partition has no room for a second copy, so this is PC only:
//
// 1. Build the new file system elsewhere (a sparse file), copying folders,
//    files, attributes, times and the header. The original is only read.
// 2. Copy it back, header last, with the signature set to "XRSZ" meanwhile.
//    The built copy never changes, so after a power cut just copy again.
//
// The partition size stays the same. Deleted entries and lost clusters are
// dropped.

#include "fatx_check.hpp"
#include <map>
#include <vector>

typedef struct
{
    uint64_t offset;
    uint64_t length;
} FatxRange;

// Records which ranges were written, for the copy back
class FatxRangeRecorder : public FatxDevice
{
public:
    explicit FatxRangeRecorder(FatxDevice &inner) : m_inner(inner) {}
    bool Read(uint64_t offset, void *buffer, uint32_t length) override;
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override;
    bool Sync() override { return m_inner.Sync(); }
    std::vector<FatxRange> Ranges() const;

private:
    FatxDevice &m_inner;
    std::map<uint64_t, uint64_t> m_ranges; // Begin to end, merged
};

enum FatxReclusterStatus
{
    FATX_RECLUSTER_DONE = 0,
    FATX_RECLUSTER_PLANNED,      // Dry run; it fits
    FATX_RECLUSTER_SAME,         // Already that cluster size
    FATX_RECLUSTER_BAD_SIZE,     // Not a power of two from 16 KB to 1 MB
    FATX_RECLUSTER_TOO_SMALL,    // Below XBpartitioner's size for the partition
    FATX_RECLUSTER_NOT_FATX,
    FATX_RECLUSTER_RESIZING,     // An unfinished resize must be finished first
    FATX_RECLUSTER_HAS_ERRORS,   // See check
    FATX_RECLUSTER_NO_ROOM,      // The files won't fit
    FATX_RECLUSTER_READ_ERROR,
    FATX_RECLUSTER_WRITE_ERROR,  // While building; the original is untouched
    FATX_RECLUSTER_BUILD_FAILED  // The result didn't check clean (a bug); not used
};

typedef struct
{
    FatxReclusterStatus status;
    uint32_t old_cluster_bytes;
    uint32_t new_cluster_bytes;
    uint64_t files;
    uint64_t folders;
    uint64_t clusters_needed;    // At the new size, folders included
    uint64_t clusters_available;
    uint64_t old_clusters_used;
    FatxCheckResult check;       // The original's check
} FatxReclusterResult;

// Step 1. Pass a NULL build for a dry run.
bool FatxBuildReclustered(FatxDevice &original, uint64_t lba_size, uint32_t new_cluster_bytes, FatxRangeRecorder *build,
                          FatxReclusterResult *result, FatxProgress *progress = NULL);

// Step 2: copies the built ranges onto the partition. Safe to repeat.
bool FatxCopyReclustered(FatxDevice &build, FatxDevice &partition, const std::vector<FatxRange> &ranges,
                         FatxProgress *progress = NULL);

#endif // FATX_RECLUSTER_H_

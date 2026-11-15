#ifndef FATX_FOLDER_H_
#define FATX_FOLDER_H_

// Reads a FATX partition's folders on the PC, for browsing. Read only: it
// never writes, and stops at anything it can't follow safely.

#include "fatx_check.hpp"
#include <string>
#include <vector>

// One entry of a folder (deleted ones are left out)
struct FatxFolderEntry
{
    std::string name; // Printable: other bytes show as '?'
    bool directory;
    uint32_t first_cluster;
    uint32_t size;
    uint16_t modified_date; // FATX format: year since 2000, month, day
    uint16_t modified_time; // Hour, minute, two second steps
    uint64_t dirent_offset; // Where its entry is, from the partition's start
};

enum FatxFolderStatus
{
    FATX_FOLDER_OK = 0,
    FATX_FOLDER_NOT_FATX,    // Unformatted, or another file system
    FATX_FOLDER_RESIZING,    // An unfinished resize: the layout is in flux
    FATX_FOLDER_BAD_HEADER,  // Invalid cluster size or root
    FATX_FOLDER_READ_ERROR,
    FATX_FOLDER_BROKEN_CHAIN // A folder's chain leaves the partition, loops or hits a marker
};

// Receives a file's contents in order
class FatxFileSink
{
  public:
    virtual ~FatxFileSink()
    {
    }
    virtual bool Write(const void *data, uint32_t length) = 0;
};

class FatxFolderReader
{
  public:
    FatxFolderReader(FatxReader &reader, uint64_t partition_bytes);

    // Reads the header; anything but FATX_FOLDER_OK means nothing can be listed
    FatxFolderStatus Open();
    uint32_t RootCluster() const
    {
        return m_root;
    }
    // The volume label from the header; characters outside ASCII show as '?'
    const std::string &Label() const
    {
        return m_label;
    }
    // A folder's entries, in the order they're stored. On failure, the
    // entries read before it are kept.
    FatxFolderStatus List(uint32_t first_cluster, std::vector<FatxFolderEntry> *entries);
    // A file's size bytes, in order. Only the clusters its size needs are
    // read, so a loop or a short chain shows up as FATX_FOLDER_BROKEN_CHAIN.
    // FATX_FOLDER_READ_ERROR also when the sink refuses a write.
    FatxFolderStatus ReadFile(const FatxFolderEntry &file, FatxFileSink &sink);

  private:
    bool NextCluster(uint32_t cluster, uint32_t *next);
    // How many clusters from first on follow each other on the disk (and in
    // the chain), up to most; *after is the cluster after them
    FatxFolderStatus FindRun(uint32_t first, uint32_t most, uint32_t *length, uint32_t *after);
    // Adds a cluster's entries; *ended once the end marker is reached
    void AddEntries(const std::vector<unsigned char> &cluster, uint64_t cluster_offset, std::vector<FatxFolderEntry> *entries, bool *ended) const;

    FatxReader &m_reader;
    uint64_t m_partition_bytes;
    FatxLayout m_layout;
    uint32_t m_cluster_bytes;
    uint32_t m_root;
    std::string m_label;
    std::vector<unsigned char> m_fat_chunk;
    uint64_t m_fat_chunk_offset;
    bool m_fat_chunk_valid;
};

// "2001-11-15 12:00", or "" when there is no date
std::string fatx_time_text(uint16_t date, uint16_t time);

// A FATX name Windows accepts as a file name: characters it forbids become
// '_', as do a trailing dot or space, and device names (CON, NUL, COM1...)
// get a '_' in front
std::string windows_file_name(const std::string &fatx_name);

#endif // FATX_FOLDER_H_

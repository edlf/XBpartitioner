#ifndef FATX_IMPORT_H_
#define FATX_IMPORT_H_

// Copies PC files and folders into a FATX volume that already holds data,
// keeping to what the Xbox can read: name, path and file size limits, the
// entries a folder may hold, and the free space, all checked before anything
// is written. Writes go through FatxWriter::WriteThrough(), so a power cut
// costs lost space at worst.

#include "fatx_writer.hpp"

#include <filesystem>
#include <string>
#include <vector>

// The Xbox's limits for what it reads
const size_t FATX_MAX_PATH_LENGTH = 240;        // From the partition's root, '\' between names
const uint32_t FATX_MAX_FOLDER_ENTRIES = 4096;  // In one folder
const uint64_t FATX_MAX_FILE_SIZE = 0xFFFFFFFF; // The size field is 32 bits

// Where the copies go
struct FatxImportTarget
{
    uint32_t cluster; // The folder's first cluster
    std::string path; // From the root: "" for the root itself, "\Games"
};

// A file or folder to add
struct FatxImportItem
{
    std::filesystem::path source;
    std::string name;
    bool directory;
    uint32_t size;
    uint16_t date; // FATX format, from the PC's modified time
    uint16_t time;
    int parent; // Index of the folder it goes in, -1 for the target
};

struct FatxImportPlan
{
    std::vector<FatxImportItem> items; // Folders before what's in them
    std::vector<std::string> skipped;  // What isn't added, and why
    uint64_t bytes = 0;
    uint64_t clusters_needed = 0;
    uint64_t clusters_free = 0;

    bool Fits() const
    {
        return clusters_needed <= clusters_free;
    }
};

// Receives a line for each problem while writing
class FatxImportLog
{
  public:
    virtual ~FatxImportLog()
    {
    }
    virtual void Note(const std::string &line) = 0;
};

struct FatxImportResult
{
    int files = 0;
    int folders = 0;
    int failed = 0;
    uint64_t bytes = 0;
};

class FatxImporter
{
  public:
    FatxImporter(FatxDevice &partition, uint64_t partition_bytes);

    // Checks the volume and reads its FAT. Empty if it can be written to,
    // else why not (a volume with errors is never written).
    std::string Open(FatxProgress *progress = NULL, uint64_t clean_fingerprint = 0);
    // After a Run() with nothing failed: the volume's still clean, with this FAT
    uint64_t Fingerprint() const
    {
        return m_writer.FatFingerprint();
    }
    FatxUsage Usage() const
    {
        return m_writer.Usage();
    }
    // Once opened
    uint32_t ClusterBytes() const
    {
        return m_writer.ClusterBytes();
    }
    // What would be added and what is skipped; nothing is written
    FatxImportPlan Plan(const FatxImportTarget &target, const std::vector<std::filesystem::path> &sources);
    // Adds a plan; one that doesn't Fits() is refused whole. progress counts bytes.
    FatxImportResult Run(const FatxImportTarget &target, const FatxImportPlan &plan, FatxImportLog &log, FatxProgress *progress = NULL);

  private:
    // A source and where it goes, while planning
    struct Placement
    {
        int parent;
        std::string parent_path; // FATX path of the folder it goes in
    };
    struct PlanState; // fatx_import.cpp

    void Visit(const std::filesystem::path &source, const Placement &place, PlanState *state);
    void AddFolder(const std::filesystem::path &source, const std::string &name, const Placement &place, PlanState *state);
    bool AddOne(const FatxImportItem &item, uint32_t parent_cluster, uint32_t *cluster, FatxProgress *progress, uint64_t *done);
    uint64_t ClustersFor(uint64_t bytes) const;

    FatxDevice &m_device;
    const uint64_t m_partition_bytes;
    FatxWriter m_writer;
    uint64_t m_total_bytes; // Of the plan being run, for the progress
};

// A FATX date and time from a PC file time (local time; 2000 if earlier)
void fatx_time_from_file_time(uint64_t file_time, uint16_t *date, uint16_t *time);

#endif // FATX_IMPORT_H_

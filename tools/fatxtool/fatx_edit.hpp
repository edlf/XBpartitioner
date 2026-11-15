#ifndef FATX_EDIT_H_
#define FATX_EDIT_H_

// Changes to a FATX volume that holds data: new folders, deleting, the volume
// label. Like the importer, only on a volume that checks clean, and written
// in an order a power cut can't break (lost space at worst).

#include "fatx_folder.hpp"
#include "fatx_writer.hpp"

#include <string>
#include <unordered_set>
#include <vector>

// The header's volume name: UTF-16 characters from offset 16
const uint32_t FATX_LABEL_LENGTH = 32;
const uint32_t FATX_LABEL_OFFSET = 16;

// Up to 42 characters the Xbox allows in a name, and the rule in words
bool fatx_name_allowed(const std::string &name);
extern const char FATX_NAME_RULE[];
// A folder entry as the Xbox writes one; first_cluster is filled in later
FatxDirent fatx_dirent(const std::string &name, bool directory, uint32_t size, uint16_t date, uint16_t time);
// Reads the FAT, checks the volume (a damaged one or an unfinished resize is
// refused) and turns on FatxWriter::WriteThrough(). Empty if it can be
// changed, else why not. The check reads the whole volume, so it's skipped
// when the FAT still has a fingerprint known to be clean (0: none known).
// progress: the check's.
std::string fatx_open_for_changes(FatxDevice &partition, uint64_t partition_bytes, FatxWriter &writer, FatxProgress *progress = NULL,
                                  uint64_t clean_fingerprint = 0);
// Printable ASCII up to 32 characters, or empty to clear it; empty if it's
// fine, else why not
std::string fatx_label_problem(const std::string &label);

class FatxEditor
{
  public:
    FatxEditor(FatxDevice &partition, uint64_t partition_bytes);

    // See fatx_open_for_changes()
    std::string Open(FatxProgress *progress = NULL, uint64_t clean_fingerprint = 0);
    // After changes that worked: the volume's still clean, with this FAT
    uint64_t Fingerprint() const
    {
        return m_writer.FatFingerprint();
    }
    FatxUsage Usage() const
    {
        return m_writer.Usage();
    }

    // Why a folder of that name can't go into parent (its first cluster;
    // parent_path from the root, "" for the root); empty if it can
    std::string NewFolderProblem(uint32_t parent, const std::string &parent_path, const std::string &name);
    bool MakeFolder(uint32_t parent, const std::string &name, uint16_t date, uint16_t time);

    // The entry and, for a folder, everything in it: the entry is marked
    // deleted first, then its clusters are freed. False if the entry isn't
    // what was listed anymore, or a write failed. *freed: clusters freed.
    bool Delete(const FatxFolderEntry &entry, uint64_t *freed);

    bool SetLabel(const std::string &label);

  private:
    // A chain's clusters, each once (a loop ends it)
    void CollectChain(uint32_t first, std::vector<uint32_t> *clusters, std::unordered_set<uint32_t> *seen) const;
    // Every cluster of an entry, and of all that's in it if it's a folder
    bool CollectTree(const FatxFolderEntry &entry, std::vector<uint32_t> *clusters);
    bool MarkDeleted(const FatxFolderEntry &entry);

    FatxDevice &m_device;
    const uint64_t m_partition_bytes;
    FatxWriter m_writer;
};

#endif // FATX_EDIT_H_

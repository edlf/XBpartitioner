#ifndef PC_IMAGE_H_
#define PC_IMAGE_H_

// PC side pieces shared by fatxtool and fatxgui: image files, their tables,
// the cluster size change's plan file and filling an image from a folder.

#include "fatx_check.hpp"
#include "fatx_recluster.hpp"
#include "fatx_resize.hpp"
#include "fatx_trim.hpp"
#include "fatx_writer.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <stdio.h>
#include <string>
#include <vector>

// E, C, X, Y, Z, then F, G, ...
char partition_letter(int index);

uint64_t file_size(FILE *file);

// A file, starting at base (a partition inside a disk image)
class FileDevice : public FatxDevice
{
  public:
    FileDevice(FILE *file, uint64_t base) : m_file(file), m_base(base)
    {
    }
    bool Read(uint64_t offset, void *buffer, uint32_t length) override;
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override;
    // Flushes past Windows' cache
    bool Sync() override;

  private:
    FILE *m_file;
    uint64_t m_base;
};

// A partition inside a disk (an image or a drive), for reading and writing
class FatxOffsetDevice : public FatxDevice
{
  public:
    FatxOffsetDevice(FatxDevice &inner, uint64_t base) : m_inner(inner), m_base(base)
    {
    }
    bool Read(uint64_t offset, void *buffer, uint32_t length) override
    {
        return m_inner.Read(m_base + offset, buffer, length);
    }
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override
    {
        return m_inner.Write(m_base + offset, buffer, length);
    }
    bool Sync() override
    {
        return m_inner.Sync();
    }

  private:
    FatxDevice &m_inner;
    uint64_t m_base;
};

// Updates backup copies of the table, found in the last sector or the
// last 4K block (only copies that match the old table)
void update_backup_tables(FatxDevice &disk, uint64_t disk_bytes, const XboxPartitionTable &old_table, const XboxPartitionTable &new_table);

// Stores a new partition size in a disk's table: sector 0, then the backup
class DiskTable : public FatxResizeTable
{
  public:
    DiskTable(FatxDevice &disk, uint64_t disk_bytes, int index) : m_disk(disk), m_disk_bytes(disk_bytes), m_index(index)
    {
    }
    bool SetSize(uint64_t new_lba_size) override;

  private:
    FatxDevice &m_disk;
    uint64_t m_disk_bytes;
    int m_index;
};

// Stores a new partition size: the file length for a partition image, or
// the table entry (and backup) for a disk image
class ImageTable : public FatxResizeTable
{
  public:
    // index -1 means a partition image
    ImageTable(FILE *file, int index) : m_file(file), m_index(index)
    {
    }
    bool SetSize(uint64_t new_lba_size) override;

  private:
    FILE *m_file;
    int m_index;
};

// ---- recluster: <image>.recluster lists what to copy from <image>.recluster.tmp

#pragma pack(push, 1)
typedef struct
{
    char magic[8];
    uint64_t partition_offset; // Bytes into the image
    uint64_t lba_size;
    uint32_t new_cluster_bytes;
    uint32_t range_count;
} ReclusterPlanHeader;
#pragma pack(pop)

extern const char RECLUSTER_MAGIC[8];

std::filesystem::path recluster_plan_path(const std::filesystem::path &image);
std::filesystem::path recluster_build_path(const std::filesystem::path &image);

// Written to a .part file first, then renamed
bool save_recluster_plan(const std::filesystem::path &path, const ReclusterPlanHeader &header, const std::vector<FatxRange> &ranges);
bool load_recluster_plan(const std::filesystem::path &path, ReclusterPlanHeader *header, std::vector<FatxRange> *ranges);
void remove_recluster_files(const std::filesystem::path &plan_path, const std::filesystem::path &build_path);

// Unwritten parts then take no space (NTFS only)
void make_sparse(FILE *file);

// ---- make

// The file name if the Xbox accepts it, otherwise empty
std::string xbox_name(const std::filesystem::path &path);

// Copies a folder into the image. note gets a line for each item skipped
// (*skipped counts them) and for a failure, which stops the copy.
bool copy_tree(FatxWriter &writer, const std::filesystem::path &from, uint32_t dir, int *skipped, const std::function<void(const std::string &)> &note);

// ---- What a command works on

// A disk or partition image, or an Xbox drive (\\.\PhysicalDriveN, needs
// administrator rights). Drives are written straight to, unbuffered.
class DiskTarget
{
  public:
    ~DiskTarget();

    static bool IsDrivePath(const std::filesystem::path &path);

    // write: for repairs and resizes; on a drive it first checks Windows
    // allows writing (sector 0 is written back as it is). ata: for TRIM.
    bool Open(const std::filesystem::path &path, bool write, bool ata, std::string *error);

    bool IsDrive() const
    {
        return m_drive != nullptr;
    }
    // The whole image or drive
    FatxDevice &Device()
    {
        return m_drive ? (FatxDevice &)*m_drive : (FatxDevice &)*m_image;
    }
    WindowsDisk *Drive()
    {
        return m_drive.get();
    }
    // Images only (NULL for a drive)
    FILE *File()
    {
        return m_file;
    }
    uint64_t Size() const
    {
        return m_size;
    }
    // The table at sector 0, or false if there is none
    bool ReadTable(XboxPartitionTable *table);
    // Where a partition's new size goes: the table (and its backup), or for
    // index -1 a partition image's length (NULL on a drive)
    std::unique_ptr<FatxResizeTable> Table(int index);

  private:
    FILE *m_file = nullptr;
    std::unique_ptr<FileDevice> m_image;
    std::unique_ptr<WindowsDisk> m_drive;
    uint64_t m_size = 0;
};

// ---- trim

// Decodes an IDENTIFY string (byte swapped)
std::string identify_string(const uint16_t *words, int count);

#endif // PC_IMAGE_H_

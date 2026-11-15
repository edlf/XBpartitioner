#ifndef FATXGUI_WORKSPACE_HPP_
#define FATXGUI_WORKSPACE_HPP_

// What's open: an image or a drive, its partitions, and what was found on it

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "drive_survey.hpp"
#include "pc_image.hpp"

namespace fatxgui
{

namespace fs = std::filesystem;

struct Part
{
    int index = -1;               // In the table, -1 for a partition image or a drive without a table
    uint64_t start = 0;           // LBAs
    uint64_t sectors = 0;
    bool fits = true;             // Inside the image or drive
    bool fatx = false;
    bool resizing = false;        // Interrupted resize (XRSZ)
    uint64_t resize_old = 0;
    uint64_t resize_new = 0;
    bool usage_ok = false;
    FatxUsage usage = {};
    uint64_t max_sectors = 0;     // How far it can grow: the next partition or the end
};

struct Source
{
    enum Type
    {
        NONE,
        IMAGE,
        DRIVE
    } type = NONE;
    fs::path path;                // Image file, or \\.\PhysicalDriveN
    std::string name;
    uint64_t bytes = 0;
    bool has_table = false;
    // Hard drive with a table; memory unit for a drive with FATX straight on
    // it; for an image without a table, what the user said it is
    FatxMedium medium = FATX_MEDIUM_UNKNOWN;
    bool medium_needed = false; // An image with FATX and no table: ask first
    std::vector<Part> parts;
    bool recluster_pending = false;
    ReclusterPlanHeader plan = {};

    // Repairs and resizes: images and drives
    bool Writable() const
    {
        return type != NONE;
    }
    // A cluster size change builds a full copy next to the image first
    bool CanRecluster() const
    {
        return type == IMAGE;
    }
    // A FATX volume straight on a drive fills it, and there's no table to
    // hold a new size; memory units have a fixed size
    bool CanResize() const
    {
        return has_table || (type == IMAGE && medium != FATX_MEDIUM_MEMORY_UNIT);
    }
    // "Xbox drive", "Memory unit image"...
    std::string Kind() const
    {
        if (medium == FATX_MEDIUM_MEMORY_UNIT)
        {
            return type == DRIVE ? "Memory unit (drive)" : "Memory unit image";
        }
        if (type == DRIVE)
        {
            return "Xbox drive";
        }
        return has_table ? "Disk image" : "Partition image";
    }
};

// TRIM: what's free on every partition, then the command
struct TrimPlan
{
    bool ready = false;
    std::vector<FatxRange> ranges; // Disk offsets
    uint64_t bytes = 0;
    bool identified = false;
    bool supported = false;
    uint32_t max_blocks = 0;
    std::string model;
};

// What's open, and what was found for it
struct Workspace
{
    Source src;
    int selected = 0;              // In src.parts
    TrimPlan trim;                 // Once the free space was found (drives)
    std::vector<DriveInfo> drives; // Xbox drives, once looked for
    bool drives_scanned = false;
    int copies_to_pc = 0;          // Counts finished copies, so the browser shows them
    int copies_to_fatx = 0;
    // Each partition's FAT fingerprint when it was last known clean, so a
    // change can skip the full check (see fatx_open_for_changes())
    std::map<std::string, uint64_t> clean_fats;
};

class SourceIo
{
  public:
    bool Open(const Source &src, bool write, bool trim, std::string *error)
    {
        if (!m_target.Open(src.path, write, trim, error))
        {
            return false;
        }
        UseMedium(src.medium);
        return true;
    }
    // Once the scan knows better than the source did
    void UseMedium(FatxMedium medium)
    {
        m_disk = std::make_unique<FatxMediumDevice>(m_target.Device(), medium);
    }
    // The whole image or drive, saying what medium it is
    FatxDevice &Disk()
    {
        return *m_disk;
    }
    // Where a partition's new size goes: the image (its table or its length),
    // or the drive's table and its backup
    std::unique_ptr<FatxResizeTable> Table(int index)
    {
        return m_target.Table(index);
    }
    WindowsDisk *Drive()
    {
        return m_target.Drive();
    }
    FILE *File()
    {
        return m_target.File();
    }
    uint64_t Size() const
    {
        return m_target.Size();
    }

  private:
    DiskTarget m_target; // Shared with fatxtool
    std::unique_ptr<FatxMediumDevice> m_disk;
};

// Reads the table and what each partition holds. On the worker thread.
bool scan_source(Source *src, FatxProgress &whole, std::string *error);

// Whether the open image or drive still has the size and partitions the scan
// found: drive numbers can change when drives come and go
bool layout_matches_scan(SourceIo &io, const Source &src);

} // namespace fatxgui

#endif // FATXGUI_WORKSPACE_HPP_

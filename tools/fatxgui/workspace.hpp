#ifndef FATXGUI_WORKSPACE_HPP_
#define FATXGUI_WORKSPACE_HPP_

// What's open: an image or a drive, its partitions, and what was found on it

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "pc_image.hpp"

namespace fatxgui
{

namespace fs = std::filesystem;

struct Part
{
    int index = -1;               // In the table, -1 for a partition image
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

struct DriveInfo
{
    int number;
    uint64_t bytes;
    int partitions;
};

// What's open, and what was found for it
struct Workspace
{
    Source src;
    int selected = 0;              // In src.parts
    TrimPlan trim;                 // Once the free space was found (drives)
    std::vector<DriveInfo> drives; // Xbox drives, once looked for
    bool drives_scanned = false;
};

class SourceIo
{
  public:
    bool Open(const Source &src, bool write, bool trim, std::string *error)
    {
        return m_target.Open(src.path, write, trim, error);
    }
    // The whole image or drive
    FatxDevice &Disk()
    {
        return m_target.Device();
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
};

// Reads the table and what each partition holds. On the worker thread.
bool scan_source(Source *src, FatxProgress &whole, std::string *error);

} // namespace fatxgui

#endif // FATXGUI_WORKSPACE_HPP_

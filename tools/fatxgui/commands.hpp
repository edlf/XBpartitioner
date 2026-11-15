#ifndef FATXGUI_COMMANDS_HPP_
#define FATXGUI_COMMANDS_HPP_

// What the buttons do. commands_*.cpp: one file per command, with its jobs.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "activity_log.hpp"
#include "fatx_folder.hpp"
#include "fatx_edit.hpp"
#include "fatx_import.hpp"
#include "xbox_hdd_format.hpp"
#include "dialogs.hpp"
#include "job_runner.hpp"
#include "workspace.hpp"

namespace fatxgui
{

// The Create image window's choices
enum NewImageKind
{
    NEW_IMAGE_XBOX_HDD = 0,  // A whole Xbox hard drive: table and partitions
    NEW_IMAGE_MEMORY_UNIT    // One FATX volume counted in 4 KB sectors
};

struct NewImage
{
    bool open = false;
    int kind = NEW_IMAGE_MEMORY_UNIT; // NewImageKind
    std::wstring path;
    double hdd_gb = 8.0;
    int layout = XBOX_HDD_F; // XboxHddLayout
    bool lba48_v4 = true;
    double mu_mb = 8.0;   // A memory unit's size
    std::wstring from;    // Memory units: a folder to fill it from
};

// Files and folders to copy out of a partition, into a folder on the PC
struct ExtractRequest
{
    Source src;
    Part part;
    std::vector<FatxFolderEntry> items;
    fs::path destination;
};

// PC files and folders to copy into a partition's folder
struct ImportRequest
{
    Source src;
    Part part;
    FatxImportTarget target;
    std::vector<fs::path> sources;
};

// A new folder in a partition's folder
struct NewFolderRequest
{
    Source src;
    Part part;
    FatxImportTarget where;
    std::string name;
};

// Entries of one of a partition's folders, folders with all that's in them
struct DeleteRequest
{
    Source src;
    Part part;
    std::vector<FatxFolderEntry> entries;
    uint32_t cluster_bytes; // For the freed space in the log
};

// "" clears it
struct LabelRequest
{
    Source src;
    Part part;
    std::string label;
};

struct CommandParts
{
    ActivityLog &log;
    JobRunner &jobs;
    Dialogs &dialogs;
    Workspace &workspace;
};

// Each command logs what it does and starts its job. The jobs report back
// through it (on the UI thread, except Log, which any thread may use).
class Commands
{
  public:
    explicit Commands(const CommandParts &parts) : m_log(parts.log), m_jobs(parts.jobs), m_dialogs(parts.dialogs), m_workspace(parts.workspace)
    {
    }

    // medium: what it is, when that's known (an image without a partition
    // table is asked about otherwise)
    void OpenImage(const fs::path &path, FatxMedium medium = FATX_MEDIUM_UNKNOWN);
    // Again, now that the user said what the image is
    void OpenImageAs(const Source &unread, FatxMedium medium);
    void OpenDrive(int number);
    void Reload();
    void ScanDrives();
    // Asks first; never a drive holding a Windows volume
    void AskFormatDrive(const DriveInfo &drive);
    // Asks twice; never a drive holding a Windows volume
    void AskFormatXboxHdd(const DriveInfo &drive, const XboxHddSpec &spec);
    void Check(const Part &part);
    void Repair(const Part &part);
    void Resize(const Part &part, uint64_t new_lba);
    void ResumeResize(const Part &part);
    void Recluster(const Part &part, uint32_t new_cluster_bytes);
    void ResumeRecluster();
    void FindTrim();
    void Trim();
    void Make(const NewImage &spec);
    void Extract(const ExtractRequest &request);
    // Checks and plans it first, then asks
    void Import(const ImportRequest &request);
    void NewFolder(const NewFolderRequest &request);
    // Asks first
    void AskDelete(const DeleteRequest &request);
    void SetLabel(const LabelRequest &request);
    // The confirmations the panels ask for
    void AskResize(const Part &part, uint64_t new_lba, FatxResizeKind kind);
    void AskTrim();

    // For the jobs and dialog actions
    ActivityLog &Log()
    {
        return m_log;
    }
    void Start(const std::string &title, bool writes, std::unique_ptr<JobWork> work)
    {
        m_jobs.Start(title, writes, std::move(work));
    }
    void Ask(DialogText text, std::unique_ptr<DialogAction> action)
    {
        m_dialogs.Ask(std::move(text), std::move(action));
    }
    void Choose(DialogText text, std::unique_ptr<DialogAction> first, std::unique_ptr<DialogAction> second)
    {
        m_dialogs.Choose(std::move(text), std::move(first), std::move(second));
    }
    void Tell(const std::string &title, std::vector<std::string> lines)
    {
        m_dialogs.Tell(title, std::move(lines));
    }
    // A source was read: it replaces what's open
    void Opened(const Source &src, int keep_selected)
    {
        m_workspace.src = src;
        m_workspace.selected = keep_selected < (int)src.parts.size() ? keep_selected : 0;
    }
    void CopiedToPc()
    {
        m_workspace.copies_to_pc++;
    }
    void CopiedToFatx()
    {
        m_workspace.copies_to_fatx++;
    }
    // The partition's FAT fingerprint when it was last known clean; 0 if none
    uint64_t CleanFat(const Source &src, const Part &part) const;
    // 0 forgets it
    void RememberCleanFat(const Source &src, const Part &part, uint64_t fingerprint);
    // After a change, from the FAT the job holds: no need to read the drive again
    void UpdateUsage(const Source &src, const Part &part, const FatxUsage &usage);
    void FoundTrim(const TrimPlan &plan)
    {
        m_workspace.trim = plan;
    }
    void FoundDrives(const std::vector<DriveInfo> &drives)
    {
        m_workspace.drives = drives;
        m_workspace.drives_scanned = true;
    }
    // They changed: looked at again when the drive window next opens
    void ForgetDrives()
    {
        m_workspace.drives.clear();
        m_workspace.drives_scanned = false;
    }

  private:
    // unread: its type, path and name
    void OpenSource(const Source &unread, int keep_selected);

    ActivityLog &m_log;
    JobRunner &m_jobs;
    Dialogs &m_dialogs;
    Workspace &m_workspace;
};

} // namespace fatxgui

#endif // FATXGUI_COMMANDS_HPP_

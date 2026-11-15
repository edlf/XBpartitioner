#ifndef FATXGUI_COMMANDS_HPP_
#define FATXGUI_COMMANDS_HPP_

// What the buttons do. commands_*.cpp: one file per command, with its jobs.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "activity_log.hpp"
#include "dialogs.hpp"
#include "job_runner.hpp"
#include "workspace.hpp"

namespace fatxgui
{

// The new image window's choices
struct NewImage
{
    bool open = false;
    std::wstring path;
    double size_mb = 1024.0;
    int cluster = 0; // Index into new_image_clusters
    std::wstring from;
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

    void OpenImage(const fs::path &path);
    void OpenDrive(int number);
    void Reload();
    void ScanDrives();
    void Check(const Part &part);
    void Repair(const Part &part);
    void Resize(const Part &part, uint64_t new_lba);
    void ResumeResize(const Part &part);
    void Recluster(const Part &part, uint32_t new_cluster_bytes);
    void ResumeRecluster();
    void FindTrim();
    void Trim();
    void Make(const NewImage &spec);
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
    void FoundTrim(const TrimPlan &plan)
    {
        m_workspace.trim = plan;
    }
    void FoundDrives(const std::vector<DriveInfo> &drives)
    {
        m_workspace.drives = drives;
        m_workspace.drives_scanned = true;
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

// Opening an image or a drive, and looking for Xbox drives

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// One answer to "memory unit or hard drive partition?"
class OpenAsAction : public DialogAction
{
  public:
    OpenAsAction(Commands &commands, const Source &unread, FatxMedium medium) : m_commands(commands), m_unread(unread), m_medium(medium)
    {
    }
    void Run() override
    {
        m_commands.OpenImageAs(m_unread, m_medium);
    }

  private:
    Commands &m_commands;
    const Source m_unread;
    const FatxMedium m_medium;
};

// Reads an image's or drive's partitions; once read, it replaces what's open
class OpenSourceWork : public JobWork
{
  public:
    OpenSourceWork(Commands &commands, const Source &unread, int keep_selected)
        : m_commands(commands), m_loaded(unread), m_keep_selected(keep_selected), m_ok(false)
    {
    }

    void Run(FatxProgress &progress) override
    {
        show_stage(progress, "Reading the partition table and how full each partition is.");
        m_ok = scan_source(&m_loaded, progress, &m_error);
    }

    void OnFinished() override
    {
        if (!m_ok)
        {
            m_commands.Log().Add(TONE_ERROR, m_loaded.name + ": " + m_error);
            m_commands.Tell("Can't open it", {m_error});
            return;
        }
        if (m_loaded.medium_needed)
        {
            AskMedium();
            return;
        }
        m_commands.Opened(m_loaded, m_keep_selected);
    }

  private:
    // The header counts clusters in 512 byte sectors on a hard drive and in
    // 4 KB ones on a memory unit, and an image doesn't say which it was
    void AskMedium()
    {
        const DialogText text = {"What is " + m_loaded.name + "?",
                                 {"It's a FATX volume without a partition table: a memory unit (or a USB",
                                  "stick used as one), or one partition copied from an Xbox hard drive.",
                                  "They count clusters differently, so it's read the way you say."},
                                 "Memory unit",
                                 "Hard drive partition"};
        m_commands.Choose(text,
                          std::make_unique<OpenAsAction>(m_commands, m_loaded, FATX_MEDIUM_MEMORY_UNIT),
                          std::make_unique<OpenAsAction>(m_commands, m_loaded, FATX_MEDIUM_HARD_DRIVE));
    }

  public:

  private:
    Commands &m_commands;
    Source m_loaded;
    int m_keep_selected;
    bool m_ok;
    std::string m_error;
};

// Every PhysicalDrive that can be read, and what's on it
class ScanDrivesWork : public JobWork
{
  public:
    explicit ScanDrivesWork(Commands &commands) : m_commands(commands)
    {
    }

    void Run(FatxProgress &progress) override
    {
        for (int n = 0; n < 32; n++)
        {
            progress.Update(n * 100 / 32);
            DriveInfo drive;
            if (survey_drive(n, &drive))
            {
                m_found.push_back(drive);
            }
        }
    }

    void OnFinished() override
    {
        m_commands.FoundDrives(m_found);
    }

  private:
    Commands &m_commands;
    std::vector<DriveInfo> m_found;
};

} // namespace

void Commands::OpenSource(const Source &unread, int keep_selected)
{
    m_jobs.Start("Reading " + unread.name, false, std::make_unique<OpenSourceWork>(*this, unread, keep_selected));
}

void Commands::OpenImage(const fs::path &path, FatxMedium medium)
{
    m_log.Add(TONE_TITLE, "Opened " + utf8(path.wstring()));
    Source unread;
    unread.type = Source::IMAGE;
    unread.path = path;
    unread.name = utf8(path.filename().wstring());
    unread.medium = medium;
    OpenSource(unread, 0);
}

void Commands::OpenDrive(int number)
{
    const std::string path = format("\\\\.\\PhysicalDrive%d", number);
    m_workspace.trim = TrimPlan();
    m_log.Add(TONE_TITLE, "Opened " + path);
    Source unread;
    unread.type = Source::DRIVE;
    unread.path = fs::path(path);
    unread.name = format("PhysicalDrive%d", number);
    OpenSource(unread, 0);
}

namespace
{

std::string clean_fat_key(const Source &src, const Part &part)
{
    return utf8(src.path.wstring()) + format(":%llu:%llu", (unsigned long long)part.start, (unsigned long long)part.sectors);
}

} // namespace

uint64_t Commands::CleanFat(const Source &src, const Part &part) const
{
    const auto found = m_workspace.clean_fats.find(clean_fat_key(src, part));
    return found == m_workspace.clean_fats.end() ? 0 : found->second;
}

void Commands::RememberCleanFat(const Source &src, const Part &part, uint64_t fingerprint)
{
    if (fingerprint == 0)
    {
        m_workspace.clean_fats.erase(clean_fat_key(src, part));
        return;
    }
    m_workspace.clean_fats[clean_fat_key(src, part)] = fingerprint;
}

void Commands::UpdateUsage(const Source &src, const Part &part, const FatxUsage &usage)
{
    if (m_workspace.src.path != src.path)
    {
        return;
    }
    for (Part &open : m_workspace.src.parts)
    {
        if (open.start == part.start && open.sectors == part.sectors)
        {
            open.usage = usage;
            open.usage_ok = true;
        }
    }
}

void Commands::OpenImageAs(const Source &unread, FatxMedium medium)
{
    Source again;
    again.type = unread.type;
    again.path = unread.path;
    again.name = unread.name;
    again.medium = medium;
    m_log.Add(TONE_DIM, medium == FATX_MEDIUM_MEMORY_UNIT ? "  Read as a memory unit." : "  Read as a hard drive partition.");
    OpenSource(again, 0);
}

// Again, keeping the selected partition
void Commands::Reload()
{
    const Source &src = m_workspace.src;
    if (src.type == Source::NONE)
    {
        return;
    }
    Source unread;
    unread.type = src.type;
    unread.path = src.path;
    unread.name = src.name;
    // What the user said an image is still holds (drives are worked out again)
    unread.medium = src.type == Source::IMAGE && !src.has_table ? src.medium : FATX_MEDIUM_UNKNOWN;
    OpenSource(unread, m_workspace.selected);
}

void Commands::ScanDrives()
{
    m_jobs.Start("Looking at the drives", false, std::make_unique<ScanDrivesWork>(*this));
}

} // namespace fatxgui

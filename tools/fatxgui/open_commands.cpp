// Opening an image or a drive, and looking for Xbox drives

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

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
        m_commands.Opened(m_loaded, m_keep_selected);
    }

  private:
    Commands &m_commands;
    Source m_loaded;
    int m_keep_selected;
    bool m_ok;
    std::string m_error;
};

// Every PhysicalDrive with an Xbox partition table (so never the Windows one)
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
            AddIfXbox(n);
        }
    }

    void OnFinished() override
    {
        m_commands.FoundDrives(m_found);
    }

  private:
    void AddIfXbox(int number)
    {
        WindowsDisk disk;
        unsigned long error = 0;
        if (!disk.Open(format("\\\\.\\PhysicalDrive%d", number).c_str(), false, &error))
        {
            return;
        }
        XboxPartitionTable table;
        if (!disk.Read(0, &table, sizeof(table)) || !PartitionTableEditor::HasMagic(table))
        {
            return;
        }
        const PartitionTableEditor editor(table, disk.Size() / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, false);
        m_found.push_back(DriveInfo{number, disk.Size(), editor.GetActivePartitionCount()});
    }

    Commands &m_commands;
    std::vector<DriveInfo> m_found;
};

} // namespace

void Commands::OpenSource(const Source &unread, int keep_selected)
{
    m_jobs.Start("Reading " + unread.name, false, std::make_unique<OpenSourceWork>(*this, unread, keep_selected));
}

void Commands::OpenImage(const fs::path &path)
{
    m_log.Add(TONE_TITLE, "Opened " + utf8(path.wstring()));
    Source unread;
    unread.type = Source::IMAGE;
    unread.path = path;
    unread.name = utf8(path.filename().wstring());
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
    OpenSource(unread, m_workspace.selected);
}

void Commands::ScanDrives()
{
    m_jobs.Start("Looking for Xbox drives", false, std::make_unique<ScanDrivesWork>(*this));
}

} // namespace fatxgui

// Copying files and folders out of a partition, to the PC. Reads the image
// or drive only; on the PC it never replaces a file that's already there.

#define NOMINMAX
#include "commands.hpp"

#include <unordered_set>

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// Something to copy, and where it goes
struct ExtractItem
{
    FatxFolderEntry entry;
    fs::path target;
};

// What happened, for the summary line
struct ExtractCounts
{
    int files = 0;
    int skipped = 0;
    int failed = 0;
    uint64_t bytes = 0;
};

// Writes a file on the PC and moves the progress by its bytes
class FileSink : public FatxFileSink
{
  public:
    FileSink(FILE *file, FatxProgress &progress, uint64_t total, uint64_t *done)
        : m_file(file), m_progress(progress), m_total(total), m_done(done)
    {
    }
    bool Write(const void *data, uint32_t length) override
    {
        if (fwrite(data, 1, length, m_file) != length)
        {
            return false;
        }
        *m_done += length;
        if (m_total > 0)
        {
            m_progress.Update((int)(*m_done * 100 / m_total));
        }
        return true;
    }

  private:
    FILE *m_file;
    FatxProgress &m_progress;
    const uint64_t m_total;
    uint64_t *m_done;
};

// The progress shared by every file of a job
struct FileProgress
{
    FatxProgress &progress;
    uint64_t total;
    uint64_t *done;
};

class ExtractWork : public JobWork
{
  public:
    ExtractWork(Commands &commands, const ExtractRequest &request) : m_commands(commands), m_log(commands.Log()), m_request(request)
    {
    }

    void OnFinished() override
    {
        m_commands.CopiedToPc();
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        std::string error;
        if (!io.Open(m_request.src, false, false, &error))
        {
            m_log.Add(TONE_ERROR, "  " + error);
            return;
        }
        FatxOffsetReader reader(io.Disk(), m_request.part.start * HDD_SECTOR_SIZE);
        FatxFolderReader folders(reader, m_request.part.sectors * HDD_SECTOR_SIZE);
        if (folders.Open() != FATX_FOLDER_OK)
        {
            m_log.Add(TONE_ERROR, "  It isn't readable FATX anymore: reload it.");
            return;
        }

        std::vector<ExtractItem> items;
        uint64_t total = 0;
        Plan(folders, &items, &total);
        Copy(folders, items, total, progress);
        Summarize();
    }

  private:
    // Every file and folder under the chosen ones, folders before what's in them
    void Plan(FatxFolderReader &folders, std::vector<ExtractItem> *items, uint64_t *total)
    {
        std::vector<ExtractItem> pending;
        for (const FatxFolderEntry &entry : m_request.items)
        {
            pending.push_back(ExtractItem{entry, m_request.destination / windows_file_name(entry.name)});
        }
        std::unordered_set<uint32_t> folders_seen; // A folder reached twice is a loop
        while (!pending.empty())
        {
            const ExtractItem item = pending.back();
            pending.pop_back();
            if (!item.entry.directory)
            {
                *total += item.entry.size;
                items->push_back(item);
                continue;
            }
            if (!folders_seen.insert(item.entry.first_cluster).second)
            {
                Note(TONE_WARN, "Skipped (the folder is reached twice): " + utf8(item.target.wstring()));
                continue;
            }
            items->push_back(item);
            AddFolderContents(folders, item, &pending);
        }
    }

    void AddFolderContents(FatxFolderReader &folders, const ExtractItem &folder, std::vector<ExtractItem> *pending)
    {
        std::vector<FatxFolderEntry> entries;
        const FatxFolderStatus status = folders.List(folder.entry.first_cluster, &entries);
        if (status != FATX_FOLDER_OK)
        {
            m_counts.failed++;
            Note(TONE_ERROR, "Couldn't read all of " + utf8(folder.target.wstring()) + " (Check shows why).");
        }
        for (const FatxFolderEntry &entry : entries)
        {
            pending->push_back(ExtractItem{entry, folder.target / windows_file_name(entry.name)});
        }
    }

    void Copy(FatxFolderReader &folders, const std::vector<ExtractItem> &items, uint64_t total, FatxProgress &progress)
    {
        uint64_t done = 0;
        for (const ExtractItem &item : items)
        {
            if (item.entry.directory)
            {
                std::error_code error;
                fs::create_directories(item.target, error);
                if (error)
                {
                    m_counts.failed++;
                    Note(TONE_ERROR, "Couldn't make the folder " + utf8(item.target.wstring()));
                }
                continue;
            }
            CopyOne(folders, item, FileProgress{progress, total, &done});
        }
    }

    void CopyOne(FatxFolderReader &folders, const ExtractItem &item, const FileProgress &shared)
    {
        std::error_code error;
        if (fs::exists(item.target, error))
        {
            m_counts.skipped++;
            *shared.done += item.entry.size;
            Note(TONE_WARN, "Skipped (already there): " + utf8(item.target.wstring()));
            return;
        }
        FILE *file = nullptr;
        if (_wfopen_s(&file, item.target.c_str(), L"wb") != 0 || file == nullptr)
        {
            m_counts.failed++;
            Note(TONE_ERROR, "Couldn't create " + utf8(item.target.wstring()));
            return;
        }
        FileSink sink(file, shared.progress, shared.total, shared.done);
        const FatxFolderStatus status = folders.ReadFile(item.entry, sink);
        const bool closed = fclose(file) == 0;
        if (status != FATX_FOLDER_OK || !closed)
        {
            fs::remove(item.target, error); // No half files
            m_counts.failed++;
            Note(TONE_ERROR,
                 (status == FATX_FOLDER_BROKEN_CHAIN ? "Damaged, not copied (Check shows why): " : "Couldn't copy: ") + utf8(item.target.wstring()));
            return;
        }
        m_counts.files++;
        m_counts.bytes += item.entry.size;
    }

    void Summarize()
    {
        std::string line = format("  Copied %d file%s (%s) to %s",
                                  m_counts.files,
                                  m_counts.files == 1 ? "" : "s",
                                  size_text(m_counts.bytes).c_str(),
                                  utf8(m_request.destination.wstring()).c_str());
        if (m_counts.skipped > 0)
        {
            line += format(", %d already there", m_counts.skipped);
        }
        if (m_counts.failed > 0)
        {
            line += format(", %d failed", m_counts.failed);
        }
        m_log.Add(m_counts.failed > 0 ? TONE_WARN : TONE_GOOD, line + ".");
    }

    // Only the first few problems are listed: a damaged folder can have many
    void Note(Tone tone, const std::string &line)
    {
        const int MAX_NOTES = 20;
        if (m_notes < MAX_NOTES)
        {
            m_log.Add(tone, "  " + line);
        }
        else if (m_notes == MAX_NOTES)
        {
            m_log.Add(TONE_DIM, "  (more problems not listed)");
        }
        m_notes++;
    }

    Commands &m_commands;
    ActivityLog &m_log;
    const ExtractRequest m_request;
    ExtractCounts m_counts;
    int m_notes = 0;
};

} // namespace

void Commands::Extract(const ExtractRequest &request)
{
    m_log.Add(TONE_TITLE, "Copying from " + partition_name(request.part.index) + " to " + utf8(request.destination.wstring()));
    m_jobs.Start("Copying files", false, std::make_unique<ExtractWork>(*this, request));
}

} // namespace fatxgui

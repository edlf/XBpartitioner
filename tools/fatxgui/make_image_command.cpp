// Creating an image: a whole Xbox hard drive (table and partitions), or a
// memory unit (one FATX volume counted in 4 KB sectors), optionally filled
// from a folder through the importer, which keeps to the Xbox's limits

#define NOMINMAX
#include "commands.hpp"

#include "fatx_format.hpp"
#include "partition_work.hpp"
#include "text.hpp"

#include <io.h>

namespace fatxgui
{

namespace
{

const uint64_t GIGABYTE = 1024ULL * 1024 * 1024;
const uint64_t MEGABYTE = 1024ULL * 1024;
// Only the first few skipped items are listed: a big folder can have many
const size_t MAX_LISTED = 20;

uint64_t image_bytes(const NewImage &spec)
{
    const double bytes = spec.kind == NEW_IMAGE_XBOX_HDD ? spec.hdd_gb * GIGABYTE : spec.mu_mb * MEGABYTE;
    return ((uint64_t)bytes) & ~(uint64_t)(FATX_HEADER_BYTES - 1);
}

uint32_t new_serial()
{
    return (uint32_t)(GetTickCount64() * 2654435761u);
}

// Logs what the fill can't write
class FillLog : public FatxImportLog
{
  public:
    explicit FillLog(ActivityLog &log) : m_log(log)
    {
    }
    void Note(const std::string &line) override
    {
        if (m_notes++ < (int)MAX_LISTED)
        {
            m_log.Add(TONE_ERROR, "  " + line);
        }
    }

  private:
    ActivityLog &m_log;
    int m_notes = 0;
};

class MakeImageWork : public JobWork
{
  public:
    MakeImageWork(Commands &commands, const NewImage &spec)
        : m_commands(commands), m_log(commands.Log()), m_spec(spec), m_path(spec.path), m_bytes(image_bytes(spec)), m_made(false)
    {
    }

    const fs::path &Path() const
    {
        return m_path;
    }

    void Run(FatxProgress &progress) override
    {
        FILE *file = CreateFile();
        if (file == nullptr)
        {
            return;
        }
        FileDevice disk(file, 0);
        m_made = (m_spec.kind == NEW_IMAGE_XBOX_HDD) ? MakeXboxHdd(disk, progress) : MakeMemoryUnit(disk, progress);
        fclose(file);
        if (!m_made)
        {
            std::error_code error;
            fs::remove(m_path, error); // Nothing half made is left behind
        }
    }

    void OnFinished() override
    {
        if (m_made)
        {
            m_commands.OpenImage(m_path, m_spec.kind == NEW_IMAGE_MEMORY_UNIT ? FATX_MEDIUM_MEMORY_UNIT : FATX_MEDIUM_HARD_DRIVE);
        }
    }

  private:
    // Sparse if the file system allows: unused clusters are never written
    FILE *CreateFile()
    {
        FILE *file = nullptr;
        if (_wfopen_s(&file, m_path.c_str(), L"w+b") != 0 || file == nullptr)
        {
            m_log.Add(TONE_ERROR, "  Can't create it.");
            return nullptr;
        }
        make_sparse(file);
        if (_chsize_s(_fileno(file), (__int64)m_bytes) != 0)
        {
            m_log.Add(TONE_ERROR, "  Can't make it that big.");
            fclose(file);
            return nullptr;
        }
        return file;
    }

    bool MakeXboxHdd(FatxDevice &disk, FatxProgress &progress)
    {
        const XboxHddSpec hdd = {(XboxHddLayout)m_spec.layout, m_spec.lba48_v4, new_serial()};
        if (!format_xbox_hdd(disk, m_bytes, hdd, &progress))
        {
            m_log.Add(TONE_ERROR, "  Writing failed.");
            return false;
        }
        const PartitionTableEditor table = xbox_hdd_table(m_bytes / HDD_SECTOR_SIZE, hdd);
        m_log.Add(TONE_GOOD,
                  format("  Made: an Xbox hard drive of %s, %d partitions, LBA48 %s table.",
                         size_text(m_bytes).c_str(),
                         table.GetActivePartitionCount(),
                         m_spec.lba48_v4 ? "v4" : "v3"));
        return true;
    }

    bool MakeMemoryUnit(FatxDevice &disk, FatxProgress &progress)
    {
        SubProgress formatting(progress, 0, m_spec.from.empty() ? 100 : 20);
        if (!FatxFormatMemoryUnit(disk, m_bytes / HDD_SECTOR_SIZE, FATX_MEMORY_UNIT_CLUSTER_BYTES, new_serial(), &formatting))
        {
            m_log.Add(TONE_ERROR, "  Writing failed.");
            return false;
        }
        m_log.Add(TONE_GOOD, format("  Made: a memory unit of %s, %u KB clusters.", size_text(m_bytes).c_str(), FATX_MEMORY_UNIT_CLUSTER_BYTES / 1024));
        if (!m_spec.from.empty())
        {
            FatxMediumDevice memory_unit(disk, FATX_MEDIUM_MEMORY_UNIT);
            SubProgress filling(progress, 20, 100);
            Fill(memory_unit, filling);
        }
        return true;
    }

    // The folder's contents into the root; what doesn't fit the Xbox's
    // limits is skipped and logged
    void Fill(FatxDevice &memory_unit, FatxProgress &progress)
    {
        std::vector<fs::path> sources;
        std::error_code error;
        for (const fs::directory_entry &item : fs::directory_iterator(fs::path(m_spec.from), error))
        {
            sources.push_back(item.path());
        }
        FatxImporter importer(memory_unit, m_bytes);
        const std::string problem = importer.Open();
        FatxFolderReader folders(memory_unit, m_bytes);
        if (!problem.empty() || error || folders.Open() != FATX_FOLDER_OK)
        {
            m_log.Add(TONE_ERROR, "  Not filled: " + (problem.empty() ? std::string("the folder couldn't be read.") : problem));
            return;
        }
        const FatxImportTarget root = {folders.RootCluster(), ""};
        const FatxImportPlan plan = importer.Plan(root, sources);
        for (size_t i = 0; i < plan.skipped.size() && i < MAX_LISTED; i++)
        {
            m_log.Add(TONE_WARN, "  " + plan.skipped[i]);
        }
        if (!plan.Fits())
        {
            m_log.Add(TONE_ERROR, "  Not filled: the folder doesn't fit. The memory unit is left empty.");
            return;
        }
        FillLog notes(m_log);
        const FatxImportResult result = importer.Run(root, plan, notes, &progress);
        m_log.Add(result.failed > 0 ? TONE_WARN : TONE_GOOD,
                  format("  Filled with %d files and %d folders (%s)%s.",
                         result.files,
                         result.folders,
                         size_text(result.bytes).c_str(),
                         result.failed > 0 ? format(", %d failed", result.failed).c_str() : ""));
    }

    Commands &m_commands;
    ActivityLog &m_log;
    const NewImage m_spec;
    const fs::path m_path;
    const uint64_t m_bytes;
    bool m_made;
};

} // namespace

void Commands::Make(const NewImage &spec)
{
    auto work = std::make_unique<MakeImageWork>(*this, spec);
    const std::string kind = spec.kind == NEW_IMAGE_XBOX_HDD ? "Xbox hard drive image " : "Memory unit image ";
    m_log.Add(TONE_TITLE, kind + utf8(work->Path().wstring()));
    const std::string title = "Making " + utf8(work->Path().filename().wstring()); // Before work is moved
    m_jobs.Start(title, true, std::move(work));
}

} // namespace fatxgui

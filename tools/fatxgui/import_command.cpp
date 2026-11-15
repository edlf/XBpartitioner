// Copying PC files and folders into a partition: a check and a plan first
// (reading only), then the question, then the copy. FatxImporter keeps to the
// Xbox's limits and never replaces what's there.

#define NOMINMAX
#include "commands.hpp"

#include <memory>

#include "dialogs.hpp"
#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// Only the first few skipped items are listed: a big folder can have many
const size_t MAX_LISTED = 20;

std::string target_text(const ImportRequest &request)
{
    return partition_name(request.part.index) + (request.target.path.empty() ? "\\" : request.target.path);
}

// The partition opened for the importer, for both jobs
class ImportJob : public PartitionWork
{
  protected:
    ImportJob(Commands &commands, const ImportRequest &request)
        : PartitionWork(commands, PartitionTarget{request.src, request.part}), m_request(request), m_clean_before(commands.CleanFat(request.src, request.part))
    {
    }

    // False, with the reason logged, if it can't be added to. The whole
    // volume is checked first, with progress.
    bool OpenImporter(IoAccess access, FatxProgress &progress)
    {
        if (!OpenIo(m_io, access))
        {
            return false;
        }
        m_device = std::make_unique<FatxOffsetDevice>(m_io.Disk(), Offset());
        m_importer = std::make_unique<FatxImporter>(*m_device, Bytes());
        show_stage(progress, m_clean_before != 0 ? "Reading the FAT (the volume is checked again if something else changed it)."
                                                 : "Checking the volume first: nothing is added to a damaged one.");
        const std::string problem = m_importer->Open(&progress, m_clean_before);
        if (!problem.empty())
        {
            m_log.Add(TONE_ERROR, "  " + problem);
            return false;
        }
        return true;
    }

    void LogSkipped(const FatxImportPlan &plan)
    {
        for (size_t i = 0; i < plan.skipped.size() && i < MAX_LISTED; i++)
        {
            m_log.Add(TONE_WARN, "  " + plan.skipped[i]);
        }
        if (plan.skipped.size() > MAX_LISTED)
        {
            m_log.Add(TONE_DIM, format("  (%u more skipped, not listed)", (unsigned int)(plan.skipped.size() - MAX_LISTED)));
        }
    }

    const ImportRequest m_request;
    const uint64_t m_clean_before;
    uint64_t m_clean_after = 0; // Known clean once opened (or after a copy that worked)
    SourceIo m_io;
    std::unique_ptr<FatxOffsetDevice> m_device;
    std::unique_ptr<FatxImporter> m_importer;
};

// Logs what the copy can't write
class ImportLog : public FatxImportLog
{
  public:
    explicit ImportLog(ActivityLog &log) : m_log(log)
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

// Writes it: plans again (things may have changed), copies, checks the result
class ImportWork : public ImportJob
{
  public:
    ImportWork(Commands &commands, const ImportRequest &request) : ImportJob(commands, request)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SubProgress checking(progress, 0, 30);
        if (!OpenImporter(IO_WRITE, checking))
        {
            return;
        }
        show_stage(progress, "Copying.");
        const FatxImportPlan plan = m_importer->Plan(m_request.target, m_request.sources);
        LogSkipped(plan);
        ImportLog notes(m_log);
        SubProgress copying(progress, 30, 100);
        const FatxImportResult result = m_importer->Run(m_request.target, plan, notes, &copying);
        m_log.Add(result.failed > 0 ? TONE_WARN : TONE_GOOD,
                  format("  Added %d file%s and %d folder%s (%s)%s.",
                         result.files,
                         result.files == 1 ? "" : "s",
                         result.folders,
                         result.folders == 1 ? "" : "s",
                         size_text(result.bytes).c_str(),
                         result.failed > 0 ? format(", %d failed", result.failed).c_str() : ""));
        m_usage = m_importer->Usage();
        m_clean_after = result.failed == 0 ? m_importer->Fingerprint() : 0;
    }

    // The browser shows them; the usage comes from the FAT the copy holds
    void OnFinished() override
    {
        m_commands.RememberCleanFat(m_src, m_part, m_clean_after);
        if (m_usage.cluster_bytes != 0)
        {
            m_commands.UpdateUsage(m_src, m_part, m_usage);
        }
        m_commands.CopiedToFatx();
    }

  private:
    FatxUsage m_usage = {};
};

class ImportAction : public DialogAction
{
  public:
    ImportAction(Commands &commands, const ImportRequest &request) : m_commands(commands), m_request(request)
    {
    }
    void Run() override
    {
        m_commands.Start("Copying to " + partition_name(m_request.part.index), true, std::make_unique<ImportWork>(m_commands, m_request));
    }

  private:
    Commands &m_commands;
    const ImportRequest m_request;
};

// Reads only: checks the volume and works out what would be added, then asks
class ImportPlanWork : public ImportJob
{
  public:
    ImportPlanWork(Commands &commands, const ImportRequest &request) : ImportJob(commands, request)
    {
    }

    void Run(FatxProgress &progress) override
    {
        m_planned = OpenImporter(IO_READ, progress);
        if (m_planned)
        {
            m_clean_after = m_importer->Fingerprint(); // Checked just now
            m_plan = m_importer->Plan(m_request.target, m_request.sources);
            LogSkipped(m_plan);
        }
    }

    void OnFinished() override
    {
        m_commands.RememberCleanFat(m_src, m_part, m_clean_after);
        if (!m_planned)
        {
            m_commands.Tell("Nothing copied", {"The partition can't be added to: the log says why."});
            return;
        }
        if (m_plan.items.empty())
        {
            m_commands.Tell("Nothing copied", {"Nothing can be added: the log lists what was skipped and why."});
            return;
        }
        if (!m_plan.Fits())
        {
            m_commands.Tell("Not enough free space", {format("It needs %s, %s is free.", Clusters(m_plan.clusters_needed).c_str(), Clusters(m_plan.clusters_free).c_str())});
            return;
        }
        Ask();
    }

  private:
    std::string Clusters(uint64_t count) const
    {
        return size_text(count * m_importer->ClusterBytes());
    }

    void Ask()
    {
        int files = 0;
        for (const FatxImportItem &item : m_plan.items)
        {
            files += item.directory ? 0 : 1;
        }
        const int folders = (int)m_plan.items.size() - files;
        std::vector<std::string> lines = {
            format("%d file%s and %d folder%s (%s) into %s.", files, files == 1 ? "" : "s", folders, folders == 1 ? "" : "s", size_text(m_plan.bytes).c_str(), target_text(m_request).c_str()),
        };
        if (!m_plan.skipped.empty())
        {
            lines.push_back(format("%u skipped (the log says why).", (unsigned int)m_plan.skipped.size()));
        }
        lines.push_back("Nothing already there is replaced.");
        m_commands.Ask(DialogText{"Copy to " + partition_name(m_request.part.index), with_warning(lines, m_request.src), "Copy"},
                       std::make_unique<ImportAction>(m_commands, m_request));
    }

    bool m_planned = false;
    FatxImportPlan m_plan;
};

} // namespace

void Commands::Import(const ImportRequest &request)
{
    m_log.Add(TONE_TITLE, "Copying to " + target_text(request));
    m_jobs.Start("Checking " + partition_name(request.part.index), false, std::make_unique<ImportPlanWork>(*this, request));
}

} // namespace fatxgui

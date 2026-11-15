// TRIM on a drive: finding the free clusters, then trimming them

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// What's free on every partition that checks clean, and whether the drive
// takes TRIM; nothing is written
class FindTrimWork : public SourceWork
{
  public:
    FindTrimWork(Commands &commands, const Source &src) : SourceWork(commands, src)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_ATA))
        {
            return;
        }
        const int count = (int)m_src.parts.size();
        for (int i = 0; i < count; i++)
        {
            progress.Update(i * 100 / count);
            AddFreeRanges(io, m_src.parts[i]);
        }
        IdentifyDrive(io);
        m_log.Add(TONE_TEXT, format("  To trim: %s in %u ranges.", size_text(m_plan.bytes).c_str(), (unsigned)m_plan.ranges.size()));
        m_plan.ready = true;
    }

    void OnFinished() override
    {
        m_commands.FoundTrim(m_plan);
    }

  private:
    void AddFreeRanges(SourceIo &io, const Part &part)
    {
        if (!part.fits)
        {
            m_log.Add(TONE_DIM, "  " + partition_name(part.index) + ": ends past the end of the disk, skipped.");
            return;
        }
        FatxOffsetReader reader(io.Disk(), part.start * HDD_SECTOR_SIZE);
        auto check = std::make_unique<FatxCheckResult>();
        std::vector<FatxRange> free_ranges;
        if (!FatxFreeRanges(reader, part.sectors * HDD_SECTOR_SIZE, &free_ranges, check.get()))
        {
            const char *why = check->status == FATX_CHECK_NOT_FATX   ? "not FATX"
                              : check->status == FATX_CHECK_RESIZING ? "an interrupted resize (finish it first)"
                              : check->status == FATX_CHECK_DONE     ? "errors (Check shows them)"
                                                                     : "could not be checked";
            m_log.Add(TONE_DIM, "  " + partition_name(part.index) + ": " + why + ", skipped.");
            return;
        }
        uint64_t free_bytes = 0;
        for (FatxRange range : free_ranges)
        {
            free_bytes += range.length;
            range.offset += part.start * HDD_SECTOR_SIZE;
            m_plan.ranges.push_back(range);
        }
        m_plan.bytes += free_bytes;
        m_log.Add(TONE_TEXT, format("  %s: %s free, in %u ranges.", partition_name(part.index).c_str(), size_text(free_bytes).c_str(), (unsigned)free_ranges.size()));
    }

    void IdentifyDrive(SourceIo &io)
    {
        uint16_t identify[256];
        m_plan.identified = io.Drive()->Identify(identify);
        if (!m_plan.identified)
        {
            m_log.Add(TONE_WARN, "  ATA commands don't get through to this drive (USB adapters often block them): it can't be trimmed from here.");
            return;
        }
        m_plan.supported = (identify[169] & 0x0001) != 0;
        m_plan.max_blocks = identify[105];
        m_plan.model = identify_string(&identify[27], 20);
        m_log.Add(TONE_TEXT, "  Drive: " + m_plan.model + (m_plan.supported ? ", supports TRIM." : ", no TRIM support."));
    }

    TrimPlan m_plan;
};

class TrimWork : public SourceWork
{
  public:
    TrimWork(Commands &commands, const Source &src, const TrimPlan &plan) : SourceWork(commands, src), m_plan(plan)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_ATA))
        {
            return;
        }
        if (io.Drive()->Trim(m_plan.ranges, m_plan.max_blocks, &progress))
        {
            m_log.Add(TONE_GOOD, "  Trimmed " + size_text(m_plan.bytes) + ".");
        }
        else
        {
            m_log.Add(TONE_ERROR, "  The drive refused the TRIM command (or the connection didn't pass it on).");
        }
    }

  private:
    const TrimPlan m_plan;
};

class TrimAction : public DialogAction
{
  public:
    explicit TrimAction(Commands &commands) : m_commands(commands)
    {
    }

    void Run() override
    {
        m_commands.Trim();
    }

  private:
    Commands &m_commands;
};

} // namespace

void Commands::FindTrim()
{
    m_log.Add(TONE_TITLE, "TRIM: finding the free space");
    m_jobs.Start("Finding free space", false, std::make_unique<FindTrimWork>(*this, m_workspace.src));
}

// What FindTrim found; it has to be found again afterwards
void Commands::Trim()
{
    m_log.Add(TONE_TITLE, "TRIM");
    m_jobs.Start("Trimming", true, std::make_unique<TrimWork>(*this, m_workspace.src, m_workspace.trim));
    m_workspace.trim = TrimPlan();
}

// The panels' TRIM now button, once the free space was found
void Commands::AskTrim()
{
    const TrimPlan &trim = m_workspace.trim;
    m_dialogs.Ask(DialogText{"TRIM",
                             {"The drive is told " + size_text(trim.bytes) + " of free clusters is unused.",
                              "Only free clusters are trimmed, never file data or the FAT.",
                              "Make sure nothing writes to the drive meanwhile."},
                             "TRIM"},
                  std::make_unique<TrimAction>(*this));
}

} // namespace fatxgui

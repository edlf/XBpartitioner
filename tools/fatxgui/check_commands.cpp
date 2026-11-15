// Check and repair

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

class CheckWork : public PartitionWork
{
  public:
    CheckWork(Commands &commands, const PartitionTarget &target) : PartitionWork(commands, target)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_READ))
        {
            return;
        }
        FatxOffsetReader reader(io.Disk(), Offset());
        auto result = std::make_unique<FatxCheckResult>();
        FatxCheck(reader, Bytes(), result.get(), &progress);
        m_log.Check(*result);
        if (result->status == FATX_CHECK_DONE && result->errors == 0)
        {
            RememberFat(io);
        }
    }

    // Clean: changes can skip their own check while the FAT stays like this
    void OnFinished() override
    {
        m_commands.RememberCleanFat(m_src, m_part, m_clean_fat);
    }

  private:
    void RememberFat(SourceIo &io)
    {
        FatxOffsetDevice device(io.Disk(), Offset());
        FatxWriter fat(device, Bytes());
        m_clean_fat = fat.Load() ? fat.FatFingerprint() : 0;
    }

    uint64_t m_clean_fat = 0;
};

// The repair itself, after the user agreed
class RepairWork : public PartitionWork
{
  public:
    RepairWork(Commands &commands, const PartitionTarget &target) : PartitionWork(commands, target)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_WRITE))
        {
            return;
        }
        FatxOffsetDevice device(io.Disk(), Offset());
        auto repair = std::make_unique<FatxRepairResult>();
        FatxRepair(device, Bytes(), repair.get(), true, &progress);
        if (repair->status == FATX_REPAIR_DONE)
        {
            m_log.Add(TONE_GOOD, "  Repaired: " + repairs_text(*repair) + ".");
            CheckAfter(device, m_part.sectors, progress);
        }
        else if (repair->status == FATX_REPAIR_STOPPED_PARTWAY)
        {
            m_log.Add(TONE_ERROR, "  The repair stopped partway (read or write error). Check it again.");
        }
        else
        {
            m_log.Add(TONE_WARN, "  Not repaired: the partition changed since the check.");
        }
    }
};

class RepairAction : public DialogAction
{
  public:
    RepairAction(Commands &commands, const PartitionTarget &target) : m_commands(commands), m_target(target)
    {
    }

    void Run() override
    {
        m_commands.Start("Repairing " + partition_name(m_target.part.index), true, std::make_unique<RepairWork>(m_commands, m_target));
    }

  private:
    Commands &m_commands;
    const PartitionTarget m_target;
};

// A dry run of the repair, then the question, if it would fix something
class RepairPlanWork : public PartitionWork
{
  public:
    RepairPlanWork(Commands &commands, const PartitionTarget &target)
        : PartitionWork(commands, target), m_plan(std::make_unique<FatxRepairResult>()), m_opened(false)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_READ))
        {
            return;
        }
        m_opened = true;
        FatxOffsetDevice device(io.Disk(), Offset());
        FatxRepair(device, Bytes(), m_plan.get(), false, &progress);
        m_log.Check(m_plan->check);
    }

    void OnFinished() override
    {
        if (!m_opened || !ReportPlan())
        {
            return;
        }
        const std::string what = repairs_text(*m_plan);
        m_log.Add(TONE_TEXT, "  Would repair: " + what + ".");
        m_commands.Ask(DialogText{"Repair " + partition_name(m_part.index), with_warning({"It would " + what + ".", "No file's contents change."}, m_src), "Repair"},
                       std::make_unique<RepairAction>(m_commands, Target()));
    }

  private:
    // Says why not, when the repair has nothing to do or can't run. True if
    // it would repair something.
    bool ReportPlan()
    {
        switch (m_plan->status)
        {
        case FATX_REPAIR_HAS_ERRORS:
            m_log.Add(TONE_ERROR, "  Not repaired: the safe repairs only run without errors.");
            m_commands.Tell("Not repaired", {"The check found errors.", "The safe repairs only run on a partition without errors."});
            return false;
        case FATX_REPAIR_NOTHING_TO_DO:
            m_log.Add(TONE_GOOD, "  Nothing to repair.");
            return false;
        case FATX_REPAIR_DONE:
            return true;
        default:
            return false; // Check() explained
        }
    }

    std::unique_ptr<FatxRepairResult> m_plan;
    bool m_opened;
};

} // namespace

void Commands::Check(const Part &part)
{
    m_log.Add(TONE_TITLE, partition_name(part.index) + ": check");
    m_jobs.Start("Checking " + partition_name(part.index), false, std::make_unique<CheckWork>(*this, PartitionTarget{m_workspace.src, part}));
}

void Commands::Repair(const Part &part)
{
    m_log.Add(TONE_TITLE, partition_name(part.index) + ": repair");
    m_jobs.Start("Checking " + partition_name(part.index), false, std::make_unique<RepairPlanWork>(*this, PartitionTarget{m_workspace.src, part}));
}

} // namespace fatxgui

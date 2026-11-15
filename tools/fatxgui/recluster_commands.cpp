// Changing the cluster size and defragmenting (images only), and finishing either

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// The last step of a cluster size change, and how an unfinished one is
// finished: the built file system copied onto the partition. Then the image
// is read again.
class ReclusterCopyWork : public PartitionWork
{
  public:
    void OnFinished() override
    {
        m_commands.Reload();
    }

  protected:
    ReclusterCopyWork(Commands &commands, const PartitionTarget &target)
        : PartitionWork(commands, target), m_plan_path(recluster_plan_path(target.src.path)), m_build_path(recluster_build_path(target.src.path))
    {
    }

    void CopyOntoPartition(FILE *image, const std::vector<FatxRange> &ranges, FatxProgress &progress)
    {
        FILE *build_file = nullptr;
        if (_wfopen_s(&build_file, m_build_path.c_str(), L"rb") != 0 || build_file == nullptr)
        {
            m_log.Add(TONE_ERROR, "  Can't open " + utf8(m_build_path.wstring()) + ": the copy can't be finished.");
            return;
        }
        FileDevice build(build_file, 0);
        FileDevice partition(image, Offset());
        m_log.Add(TONE_TEXT, "  Copying it onto the partition...");
        const bool copied = FatxCopyReclustered(build, partition, ranges, &progress);
        fclose(build_file);
        if (!copied)
        {
            m_log.Add(TONE_ERROR, "  Stopped partway (read or write error): use Finish to copy again.");
            return;
        }
        remove_recluster_files(m_plan_path, m_build_path);
        m_log.Add(TONE_GOOD, "  Done.");
        CheckAfter(partition, m_part.sectors, progress);
    }

    const fs::path m_plan_path;
    const fs::path m_build_path;
};

// Builds the new file system in <image>.recluster.tmp, saves the plan, then
// copies it onto the partition
class ReclusterWork : public ReclusterCopyWork
{
  public:
    ReclusterWork(Commands &commands, const PartitionTarget &target, uint32_t new_cluster_bytes)
        : ReclusterCopyWork(commands, target), m_new_cluster_bytes(new_cluster_bytes)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_WRITE))
        {
            return;
        }
        FILE *build_file = nullptr;
        if (_wfopen_s(&build_file, m_build_path.c_str(), L"w+b") != 0 || build_file == nullptr)
        {
            m_log.Add(TONE_ERROR, "  Can't create " + utf8(m_build_path.wstring()));
            return;
        }
        make_sparse(build_file);
        const bool built = Build(io, build_file, progress);
        fclose(build_file);
        if (!built)
        {
            remove_recluster_files(m_plan_path, m_build_path);
            return;
        }
        if (!SavePlan())
        {
            m_log.Add(TONE_ERROR, "  Couldn't write the plan file. The image wasn't changed.");
            remove_recluster_files(m_plan_path, m_build_path);
            return;
        }
        SubProgress copy_progress(progress, 60, 100);
        CopyOntoPartition(io.File(), m_ranges, copy_progress);
    }

  private:
    // True if it built and checked clean; m_ranges is what the copy writes
    bool Build(SourceIo &io, FILE *build_file, FatxProgress &progress)
    {
        FatxOffsetDevice device(io.Disk(), Offset());
        FileDevice build(build_file, 0);
        FatxRangeRecorder recorder(build);
        auto result = std::make_unique<FatxReclusterResult>();
        SubProgress build_progress(progress, 0, 60);
        FatxBuildReclustered(device, m_part.sectors, m_new_cluster_bytes, &recorder, result.get(), &build_progress);
        m_log.Recluster(*result, m_new_cluster_bytes, m_part.sectors);
        if (result->status != FATX_RECLUSTER_DONE)
        {
            return false;
        }
        m_ranges = recorder.Ranges();
        return true;
    }

    // So Finish can repeat the copy after a power cut
    bool SavePlan() const
    {
        ReclusterPlanHeader plan = {};
        memcpy(plan.magic, RECLUSTER_MAGIC, sizeof(RECLUSTER_MAGIC));
        plan.partition_offset = Offset();
        plan.lba_size = m_part.sectors;
        plan.new_cluster_bytes = m_new_cluster_bytes;
        plan.range_count = (uint32_t)m_ranges.size();
        return save_recluster_plan(m_plan_path, plan, m_ranges);
    }

    const uint32_t m_new_cluster_bytes;
    std::vector<FatxRange> m_ranges;
};

// Repeats the copy of a cluster size change that didn't finish
class ResumeReclusterWork : public ReclusterCopyWork
{
  public:
    ResumeReclusterWork(Commands &commands, const PartitionTarget &target) : ReclusterCopyWork(commands, target)
    {
    }

    void Run(FatxProgress &progress) override
    {
        ReclusterPlanHeader plan;
        std::vector<FatxRange> ranges;
        if (!load_recluster_plan(m_plan_path, &plan, &ranges))
        {
            m_log.Add(TONE_ERROR, "  The plan file can't be read.");
            return;
        }
        SourceIo io;
        if (!OpenIo(io, IO_WRITE))
        {
            return;
        }
        CopyOntoPartition(io.File(), ranges, progress);
    }
};

// A cluster size to change to; the one it has defragments
struct ReclusterChoice
{
    uint32_t new_cluster_bytes;
    bool defrag;
};

class ReclusterAction : public DialogAction
{
  public:
    ReclusterAction(Commands &commands, const PartitionTarget &target, const ReclusterChoice &choice)
        : m_commands(commands), m_target(target), m_choice(choice)
    {
    }

    void Run() override
    {
        m_commands.Start((m_choice.defrag ? "Defragmenting " : "Changing the cluster size of ") + partition_name(m_target.part.index),
                         true,
                         std::make_unique<ReclusterWork>(m_commands, m_target, m_choice.new_cluster_bytes));
    }

  private:
    Commands &m_commands;
    const PartitionTarget m_target;
    const ReclusterChoice m_choice;
};

// Whether it fits (nothing written), then the question
class ReclusterPlanWork : public PartitionWork
{
  public:
    ReclusterPlanWork(Commands &commands, const PartitionTarget &target, const ReclusterChoice &choice)
        : PartitionWork(commands, target), m_choice(choice), m_planned(false)
    {
    }

    void Run(FatxProgress &progress) override
    {
        SourceIo io;
        if (!OpenIo(io, IO_READ))
        {
            return;
        }
        FatxOffsetDevice device(io.Disk(), Offset());
        auto result = std::make_unique<FatxReclusterResult>();
        FatxBuildReclustered(device, m_part.sectors, m_choice.new_cluster_bytes, nullptr, result.get(), &progress);
        if (m_choice.defrag && result->check.status == FATX_CHECK_DONE)
        {
            m_log.Fragmentation(result->check);
        }
        // No file fragmented: nothing to rebuild
        if (m_choice.defrag && result->status == FATX_RECLUSTER_PLANNED && result->check.fragmented_files == 0)
        {
            m_log.Add(TONE_GOOD, "  Nothing to defragment.");
            return;
        }
        m_log.Recluster(*result, m_choice.new_cluster_bytes, m_part.sectors);
        m_planned = result->status == FATX_RECLUSTER_PLANNED;
    }

    void OnFinished() override
    {
        if (!m_planned)
        {
            return;
        }
        m_commands.Ask(DialogText{m_choice.defrag ? "Defragment" : "Change the cluster size", Question(), m_choice.defrag ? "Defragment" : "Change it"},
                       std::make_unique<ReclusterAction>(m_commands, Target(), m_choice));
    }

  private:
    std::vector<std::string> Question() const
    {
        const fs::path build_path = recluster_build_path(m_src.path);
        const std::string name = partition_name(m_part.index);
        const std::string first = m_choice.defrag ? format("Every file of %s is rewritten unfragmented and kept,", name.c_str())
                                                  : format("%s gets %u KB clusters. Every file is kept,", name.c_str(), m_choice.new_cluster_bytes / 1024);
        return {first,
                "but every byte of the partition is rewritten.",
                "The new file system is built in " + utf8(build_path.filename().wstring()) + " next to",
                "the image first: that needs free space for the files on it."};
    }

    const ReclusterChoice m_choice;
    bool m_planned;
};

} // namespace

// The cluster size it has now defragments: every file is rewritten unfragmented
void Commands::Recluster(const Part &part, uint32_t new_cluster_bytes)
{
    const ReclusterChoice choice = {new_cluster_bytes, new_cluster_bytes == part.usage.cluster_bytes};
    m_log.Add(TONE_TITLE, partition_name(part.index) + (choice.defrag ? ": defragment" : format(": change to %u KB clusters", new_cluster_bytes / 1024)));
    m_jobs.Start("Checking " + partition_name(part.index), false, std::make_unique<ReclusterPlanWork>(*this, PartitionTarget{m_workspace.src, part}, choice));
}

// For the partition the plan file names
void Commands::ResumeRecluster()
{
    const Source &src = m_workspace.src;
    const Part *found = nullptr;
    for (const Part &part : src.parts)
    {
        if (part.start * HDD_SECTOR_SIZE == src.plan.partition_offset && part.sectors == src.plan.lba_size)
        {
            found = &part;
        }
    }
    if (found == nullptr)
    {
        m_dialogs.Tell("Can't finish it", {"The unfinished cluster size change was for a partition", "that isn't in this image's table anymore."});
        return;
    }
    m_log.Add(TONE_TITLE, partition_name(found->index) + ": finish the cluster size change");
    m_jobs.Start("Finishing the cluster size change", true, std::make_unique<ResumeReclusterWork>(*this, PartitionTarget{src, *found}));
}

} // namespace fatxgui

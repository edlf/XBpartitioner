// Resizing, keeping the data, and finishing an interrupted resize

#define NOMINMAX
#include "commands.hpp"

#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// Only the size changes when the FAT can stay as it is; otherwise clusters
// move and the FAT is renumbered. Then the image or drive is read again.
class ResizeWork : public PartitionWork
{
  public:
    ResizeWork(Commands &commands, const PartitionTarget &target, uint64_t new_lba) : PartitionWork(commands, target), m_new_lba(new_lba)
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
        std::unique_ptr<FatxResizeTable> table = io.Table(m_part.index);

        auto resize = std::make_unique<FatxResizeResult>();
        SubProgress check_progress(progress, 0, 20);
        FatxCheckResize(device, m_part.sectors, m_new_lba, resize.get(), &check_progress);
        switch (resize->status)
        {
        case FATX_RESIZE_OK:
        case FATX_RESIZE_NO_FILE_SYSTEM:
            ChangeSizeOnly(device, *table, progress);
            break;
        case FATX_RESIZE_OUT_OF_RANGE:
            MoveClusters(device, *table, progress);
            break;
        default:
            m_log.ResizeRefusal(*resize);
            break;
        }
    }

    void OnFinished() override
    {
        m_commands.Reload();
    }

  private:
    void ChangeSizeOnly(FatxDevice &device, FatxResizeTable &table, FatxProgress &progress)
    {
        if (!table.SetSize(m_new_lba))
        {
            m_log.Add(TONE_ERROR, "  Writing the new size failed.");
            return;
        }
        m_log.Add(TONE_GOOD, "  Resized: only the size changed.");
        CheckAfter(device, m_new_lba, progress);
    }

    void MoveClusters(FatxDevice &device, FatxResizeTable &table, FatxProgress &progress)
    {
        auto move = std::make_unique<FatxMoveResult>();
        SubProgress move_progress(progress, 20, 100);
        FatxResizeWithMoves(device, m_part.sectors, m_new_lba, table, true, move.get(), &move_progress);
        m_log.Move(*move);
        if (move->status == FATX_MOVE_DONE)
        {
            CheckAfter(device, move->new_lba_size, progress);
        }
    }

    const uint64_t m_new_lba;
};

class ResizeAction : public DialogAction
{
  public:
    ResizeAction(Commands &commands, const Part &part, uint64_t new_lba) : m_commands(commands), m_part(part), m_new_lba(new_lba)
    {
    }

    void Run() override
    {
        m_commands.Resize(m_part, m_new_lba);
    }

  private:
    Commands &m_commands;
    const Part m_part;
    const uint64_t m_new_lba;
};

// Finishes a resize a power cut or an error stopped
class ResumeResizeWork : public PartitionWork
{
  public:
    ResumeResizeWork(Commands &commands, const PartitionTarget &target) : PartitionWork(commands, target)
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
        std::unique_ptr<FatxResizeTable> table = io.Table(m_part.index);
        auto move = std::make_unique<FatxMoveResult>();
        if (FatxResumeResize(device, *table, move.get(), &progress))
        {
            m_log.Add(TONE_GOOD, "  Finished.");
            CheckAfter(device, move->new_lba_size, progress);
        }
        else
        {
            m_log.Add(TONE_ERROR, "  Could not be finished (read or write error). Try again.");
        }
    }

    void OnFinished() override
    {
        m_commands.Reload();
    }
};

} // namespace

void Commands::Resize(const Part &part, uint64_t new_lba)
{
    m_log.Add(TONE_TITLE, partition_name(part.index) + ": resize from " + megabytes_text(part.sectors) + " to " + megabytes_text(new_lba));
    m_jobs.Start("Resizing " + partition_name(part.index), true, std::make_unique<ResizeWork>(*this, PartitionTarget{m_workspace.src, part}, new_lba));
}

void Commands::ResumeResize(const Part &part)
{
    m_log.Add(TONE_TITLE, partition_name(part.index) + ": finish the resize");
    m_jobs.Start("Finishing the resize of " + partition_name(part.index), true, std::make_unique<ResumeResizeWork>(*this, PartitionTarget{m_workspace.src, part}));
}

// The panels' Resize button
void Commands::AskResize(const Part &part, uint64_t new_lba, FatxResizeKind kind)
{
    m_dialogs.Ask(DialogText{"Resize " + partition_name(part.index),
                             with_warning({"From " + megabytes_text(part.sectors) + " to " + megabytes_text(new_lba) + ".",
                                           kind == FATX_RESIZE_KIND_MOVES ? "Clusters move and the FAT is renumbered." : "Only the size is written.",
                                           "If it stops partway, Finish resize completes it."},
                                          m_workspace.src),
                             "Resize"},
                  std::make_unique<ResizeAction>(*this, part, new_lba));
}

} // namespace fatxgui

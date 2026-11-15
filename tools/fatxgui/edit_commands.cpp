// Changes in the file browser's partition: a new folder, deleting, the volume
// label. Each runs as a job that checks the volume first (FatxEditor::Open)
// and writes nothing to a damaged one. The check is skipped while the FAT is
// as it was last known clean, and the usage comes from the FAT in memory, so
// a change only reads the FAT and writes what it changes.

#define NOMINMAX
#include "commands.hpp"

#include <windows.h>

#include "dialogs.hpp"
#include "partition_work.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// Now, as FATX keeps it
void fatx_now(uint16_t *date, uint16_t *time)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    fatx_time_from_file_time((uint64_t)now.dwHighDateTime << 32 | now.dwLowDateTime, date, time);
}

// The partition opened for changes, for each of the jobs
class EditWork : public PartitionWork
{
  protected:
    EditWork(Commands &commands, const Source &src, const Part &part)
        : PartitionWork(commands, PartitionTarget{src, part}), m_clean_before(commands.CleanFat(src, part))
    {
    }

    // False, with the reason logged, if it can't be changed. The whole
    // volume is checked first (the first 90% of the progress), unless its
    // FAT is as it was when last known clean.
    bool OpenEditor(FatxProgress &progress)
    {
        if (!OpenIo(m_io, IO_WRITE))
        {
            return false;
        }
        m_device = std::make_unique<FatxOffsetDevice>(m_io.Disk(), Offset());
        m_editor = std::make_unique<FatxEditor>(*m_device, Bytes());
        show_stage(progress, m_clean_before != 0 ? "Reading the FAT (the volume is checked again if something else changed it)."
                                                 : "Checking the volume first: nothing is changed on a damaged one.");
        SubProgress checking(progress, 0, 90);
        const std::string problem = m_editor->Open(&checking, m_clean_before);
        if (!problem.empty())
        {
            m_log.Add(TONE_ERROR, "  " + problem);
            return false;
        }
        return true;
    }

    // At the end of Run(): the FAT the editor now holds tells the usage, and
    // while every change worked, the volume is still clean
    void Settle(bool all_worked)
    {
        m_usage = m_editor->Usage();
        m_have_usage = true;
        m_clean_after = all_worked ? m_editor->Fingerprint() : 0;
    }

    // The browser shows it, and the partition's usage is updated
    void OnFinished() override
    {
        m_commands.RememberCleanFat(m_src, m_part, m_clean_after);
        if (m_have_usage)
        {
            m_commands.UpdateUsage(m_src, m_part, m_usage);
        }
        m_commands.CopiedToFatx();
    }

    SourceIo m_io;
    std::unique_ptr<FatxOffsetDevice> m_device;
    std::unique_ptr<FatxEditor> m_editor;

  private:
    const uint64_t m_clean_before;
    uint64_t m_clean_after = 0; // 0: not known clean, forgotten
    FatxUsage m_usage = {};
    bool m_have_usage = false;
};

class NewFolderWork : public EditWork
{
  public:
    explicit NewFolderWork(Commands &commands, const NewFolderRequest &request) : EditWork(commands, request.src, request.part), m_request(request)
    {
    }

    void Run(FatxProgress &progress) override
    {
        if (!OpenEditor(progress))
        {
            return;
        }
        show_stage(progress, "Making the folder.");
        const std::string problem = m_editor->NewFolderProblem(m_request.where.cluster, m_request.where.path, m_request.name);
        if (!problem.empty())
        {
            m_log.Add(TONE_ERROR, "  " + problem);
            Settle(true); // Nothing was written
            return;
        }
        uint16_t date, time;
        fatx_now(&date, &time);
        const bool made = m_editor->MakeFolder(m_request.where.cluster, m_request.name, date, time);
        m_log.Add(made ? TONE_GOOD : TONE_ERROR, made ? "  Made." : "  Writing failed: Check shows whether it left lost space.");
        Settle(made);
    }

  private:
    const NewFolderRequest m_request;
};

class DeleteWork : public EditWork
{
  public:
    DeleteWork(Commands &commands, const DeleteRequest &request) : EditWork(commands, request.src, request.part), m_request(request)
    {
    }

    void Run(FatxProgress &progress) override
    {
        if (!OpenEditor(progress))
        {
            return;
        }
        show_stage(progress, "Deleting.");
        uint64_t freed_total = 0;
        int deleted = 0;
        for (size_t i = 0; i < m_request.entries.size(); i++)
        {
            const FatxFolderEntry &entry = m_request.entries[i];
            uint64_t freed;
            if (m_editor->Delete(entry, &freed))
            {
                deleted++;
                freed_total += freed;
            }
            else
            {
                m_log.Add(TONE_ERROR, "  Not deleted (changed since it was listed, or a write failed): " + entry.name);
            }
            progress.Update(90 + (int)((i + 1) * 10 / m_request.entries.size()));
        }
        const bool all = deleted == (int)m_request.entries.size();
        m_log.Add(all ? TONE_GOOD : TONE_WARN,
                  format("  Deleted %d of %u, %s freed.", deleted, (unsigned int)m_request.entries.size(), size_text(freed_total * m_request.cluster_bytes).c_str()));
        Settle(all);
    }

  private:
    const DeleteRequest m_request;
};

class LabelWork : public EditWork
{
  public:
    LabelWork(Commands &commands, const LabelRequest &request) : EditWork(commands, request.src, request.part), m_request(request)
    {
    }

    void Run(FatxProgress &progress) override
    {
        if (!OpenEditor(progress))
        {
            return;
        }
        show_stage(progress, "Writing the label.");
        const bool written = m_editor->SetLabel(m_request.label);
        if (written)
        {
            m_log.Add(TONE_GOOD, m_request.label.empty() ? std::string("  Label cleared.") : "  Label set: " + m_request.label);
        }
        else
        {
            m_log.Add(TONE_ERROR, "  Writing the header failed.");
        }
        Settle(written);
    }

  private:
    const LabelRequest m_request;
};

class DeleteAction : public DialogAction
{
  public:
    DeleteAction(Commands &commands, const DeleteRequest &request) : m_commands(commands), m_request(request)
    {
    }
    void Run() override
    {
        m_commands.Start("Deleting", true, std::make_unique<DeleteWork>(m_commands, m_request));
    }

  private:
    Commands &m_commands;
    const DeleteRequest m_request;
};

} // namespace

void Commands::NewFolder(const NewFolderRequest &request)
{
    m_log.Add(TONE_TITLE, "New folder " + partition_name(request.part.index) + request.where.path + "\\" + request.name);
    m_jobs.Start("Making a folder", true, std::make_unique<NewFolderWork>(*this, request));
}

void Commands::AskDelete(const DeleteRequest &request)
{
    int folders = 0;
    for (const FatxFolderEntry &entry : request.entries)
    {
        folders += entry.directory ? 1 : 0;
    }
    const int files = (int)request.entries.size() - folders;
    std::vector<std::string> lines;
    if (request.entries.size() == 1)
    {
        lines.push_back("Delete " + request.entries[0].name + (folders == 1 ? " and everything in it?" : "?"));
    }
    else
    {
        lines.push_back(format("Delete %d file%s and %d folder%s%s?", files, files == 1 ? "" : "s", folders, folders == 1 ? "" : "s",
                               folders > 0 ? " with everything in them" : ""));
    }
    lines.push_back("They can't be brought back here.");
    m_log.Add(TONE_TITLE, "Deleting on " + partition_name(request.part.index));
    Ask(DialogText{"Delete", with_warning(lines, request.src), "Delete"}, std::make_unique<DeleteAction>(*this, request));
}

void Commands::SetLabel(const LabelRequest &request)
{
    m_log.Add(TONE_TITLE, "Volume label of " + partition_name(request.part.index));
    m_jobs.Start("Writing the label", true, std::make_unique<LabelWork>(*this, request));
}

} // namespace fatxgui

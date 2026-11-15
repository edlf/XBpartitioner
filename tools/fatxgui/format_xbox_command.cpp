// Formatting a whole drive as an Xbox hard drive: a partition table and FATX
// on every partition. Never a drive holding a Windows volume; asked about
// twice, and checked again right before writing.

#define NOMINMAX
#include "commands.hpp"

#include "dialogs.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

// "E 4.88 GB, C 500.00 MB, X 750.00 MB, ..."
std::string partitions_text(const DriveInfo &drive, const XboxHddSpec &spec)
{
    const PartitionTableEditor table = xbox_hdd_table(drive.bytes / HDD_SECTOR_SIZE, spec);
    std::string text;
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (table.IsPartitionEnabled(i))
        {
            text += (text.empty() ? "" : ", ") + std::string(1, partition_letter(i)) + " " + size_text(table.GetPartitionSize(i) * HDD_SECTOR_SIZE);
        }
    }
    return text;
}

// Why it may not be formatted now, as it is; empty if it may
std::string refusal_now(const DriveInfo &asked, const DriveInfo &now)
{
    if (now.bytes != asked.bytes || now.model != asked.model)
    {
        return "It isn't the drive that was picked anymore: not touched.";
    }
    const std::string refusal = xbox_format_refusal(now);
    return refusal.empty() ? refusal : refusal + " Not touched.";
}

class FormatXboxHddWork : public JobWork
{
  public:
    FormatXboxHddWork(Commands &commands, const DriveInfo &drive, const XboxHddSpec &spec)
        : m_commands(commands), m_log(commands.Log()), m_drive(drive), m_spec(spec)
    {
    }

    void Run(FatxProgress &progress) override
    {
        // Drives come and go: the number must still be the same drive
        DriveInfo now;
        if (!survey_drive(m_drive.number, &now))
        {
            m_log.Add(TONE_ERROR, "  It can't be read anymore: not touched.");
            return;
        }
        const std::string refusal = refusal_now(m_drive, now);
        if (!refusal.empty())
        {
            m_log.Add(TONE_ERROR, "  " + refusal);
            return;
        }

        DiskTarget target;
        std::string error;
        if (!target.Open(format("\\\\.\\PhysicalDrive%d", m_drive.number), true, false, &error))
        {
            m_log.Add(TONE_ERROR, "  " + error);
            return;
        }
        m_formatted = format_xbox_hdd(target.Device(), target.Size(), m_spec, &progress);
        refresh_drive_layout(m_drive.number);
        if (!m_formatted)
        {
            m_log.Add(TONE_ERROR, "  Writing failed: the drive has no usable Xbox table. If Windows refused it, remove its partitions in Disk Management first.");
            return;
        }
        m_log.Add(TONE_GOOD, "  Formatted: " + partitions_text(m_drive, m_spec) + ". Put a dashboard on C before booting from it.");
    }

    void OnFinished() override
    {
        m_commands.ForgetDrives();
        if (m_formatted)
        {
            m_commands.OpenDrive(m_drive.number);
        }
    }

  private:
    Commands &m_commands;
    ActivityLog &m_log;
    const DriveInfo m_drive;
    const XboxHddSpec m_spec;
    bool m_formatted = false;
};

// The second answer: starts it
class StartFormatAction : public DialogAction
{
  public:
    StartFormatAction(Commands &commands, const DriveInfo &drive, const XboxHddSpec &spec) : m_commands(commands), m_drive(drive), m_spec(spec)
    {
    }
    void Run() override
    {
        m_commands.Log().Add(TONE_TITLE, "Formatting " + drive_label(m_drive) + " as an Xbox hard drive");
        m_commands.Start(format("Formatting PhysicalDrive%d", m_drive.number), true, std::make_unique<FormatXboxHddWork>(m_commands, m_drive, m_spec));
    }

  private:
    Commands &m_commands;
    const DriveInfo m_drive;
    const XboxHddSpec m_spec;
};

// The first answer: asks once more
class ConfirmAgainAction : public DialogAction
{
  public:
    ConfirmAgainAction(Commands &commands, const DriveInfo &drive, const XboxHddSpec &spec) : m_commands(commands), m_drive(drive), m_spec(spec)
    {
    }
    void Run() override
    {
        const std::vector<std::string> lines = {
            "Really erase " + drive_label(m_drive) + "?",
            "Every file on it is lost, and this can't be undone.",
        };
        m_commands.Ask(DialogText{"Last chance", lines, "Yes, erase and format"}, std::make_unique<StartFormatAction>(m_commands, m_drive, m_spec));
    }

  private:
    Commands &m_commands;
    const DriveInfo m_drive;
    const XboxHddSpec m_spec;
};

} // namespace

void Commands::AskFormatXboxHdd(const DriveInfo &drive, const XboxHddSpec &spec)
{
    const std::string refusal = refusal_now(drive, drive);
    if (!refusal.empty())
    {
        Tell("Not formatted", {drive_label(drive), refusal});
        return;
    }
    std::vector<std::string> lines = drive_loss_lines(drive);
    lines.push_back("New layout: " + partitions_text(drive, spec) + ".");
    lines.push_back(spec.lba48_v4 ? "LBA48 v4 table: needs a v4 BIOS such as Cerbios." : "LBA48 v3 table: partitions up to 1 TB, the drive used up to 2 TB.");
    lines.push_back("Every partition gets an empty FATX file system; C needs a dashboard before the Xbox boots from it.");
    if (drive.data_at_start)
    {
        lines.push_back("Its first 4 MB hold data, which may be lost.");
    }
    Ask(DialogText{format("Format PhysicalDrive%d as an Xbox hard drive", drive.number), lines, "Continue..."},
        std::make_unique<ConfirmAgainAction>(*this, drive, spec));
}

} // namespace fatxgui

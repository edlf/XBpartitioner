// Formatting a whole drive as one FATX volume with no partition table, the
// layout of a memory unit. Never a drive holding a Windows volume (which
// covers the one Windows runs from); asked about first, and checked again
// right before writing.

#define NOMINMAX
#include "commands.hpp"

#include "dialogs.hpp"
#include "fatx_format.hpp"
#include "text.hpp"

namespace fatxgui
{

namespace
{

std::string drive_name(const DriveInfo &drive)
{
    return format("PhysicalDrive%d", drive.number);
}


// Why it may not be formatted now, as it is; empty if it may
std::string refusal_now(const DriveInfo &asked, const DriveInfo &now)
{
    if (now.bytes != asked.bytes || now.model != asked.model)
    {
        return "It isn't the drive that was picked anymore: not touched.";
    }
    const std::string refusal = format_refusal(now);
    return refusal.empty() ? refusal : refusal + " Not touched.";
}

class FormatDriveWork : public JobWork
{
  public:
    FormatDriveWork(Commands &commands, const DriveInfo &drive) : m_commands(commands), m_log(commands.Log()), m_drive(drive)
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
        const uint64_t sectors = target.Size() / HDD_SECTOR_SIZE;
        const uint32_t cluster_bytes = FATX_MEMORY_UNIT_CLUSTER_BYTES;
        const uint32_t serial = (uint32_t)(GetTickCount64() * 2654435761u);
        m_formatted = FatxFormatMemoryUnit(target.Device(), sectors, cluster_bytes, serial, &progress);
        refresh_drive_layout(m_drive.number);
        if (!m_formatted)
        {
            m_log.Add(TONE_ERROR, "  Writing failed. If Windows refused it, remove the drive's partitions in Disk Management first.");
            return;
        }
        m_log.Add(TONE_GOOD, format("  Formatted: one FATX volume of %s, %u KB clusters, no partition table.", size_text(target.Size()).c_str(), cluster_bytes / 1024));
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
    bool m_formatted = false;
};

class FormatDriveAction : public DialogAction
{
  public:
    FormatDriveAction(Commands &commands, const DriveInfo &drive) : m_commands(commands), m_drive(drive)
    {
    }
    void Run() override
    {
        m_commands.Log().Add(TONE_TITLE, "Formatting " + drive_label(m_drive));
        m_commands.Start("Formatting " + drive_name(m_drive), true, std::make_unique<FormatDriveWork>(m_commands, m_drive));
    }

  private:
    Commands &m_commands;
    const DriveInfo m_drive;
};

} // namespace

void Commands::AskFormatDrive(const DriveInfo &drive)
{
    const std::string refusal = refusal_now(drive, drive);
    if (!refusal.empty())
    {
        Tell("Not formatted", {drive_label(drive), refusal});
        return;
    }
    std::vector<std::string> lines = drive_loss_lines(drive);
    lines.push_back(format("It becomes one FATX volume with %u KB clusters and no partition table: the layout of a memory unit,",
                           FATX_MEMORY_UNIT_CLUSTER_BYTES / 1024));
    lines.push_back("not of an Xbox hard drive. Back up anything on it first.");
    Ask(DialogText{"Format " + drive_name(drive) + " as FATX", lines, "Erase and format"}, std::make_unique<FormatDriveAction>(*this, drive));
}

} // namespace fatxgui

#ifndef FATXGUI_PARTITION_WORK_HPP_
#define FATXGUI_PARTITION_WORK_HPP_

// What the commands' jobs share

#include "commands.hpp"

namespace fatxgui
{

enum IoAccess
{
    IO_READ,
    IO_WRITE,
    IO_ATA // Reading, and ATA commands to the drive (TRIM)
};

// Work on what's open, as it was when the job started
class SourceWork : public JobWork
{
  protected:
    SourceWork(Commands &commands, const Source &src) : m_commands(commands), m_log(commands.Log()), m_src(src)
    {
    }

    // False, with the reason logged, if it can't be opened
    bool OpenIo(SourceIo &io, IoAccess access)
    {
        std::string error;
        if (io.Open(m_src, access == IO_WRITE, access == IO_ATA, &error))
        {
            return true;
        }
        m_log.Add(TONE_ERROR, "  " + error);
        return false;
    }

    Commands &m_commands;
    ActivityLog &m_log;
    const Source m_src;
};

// A partition, and what it's on
struct PartitionTarget
{
    Source src;
    Part part;
};

// Work on one partition
class PartitionWork : public SourceWork
{
  protected:
    PartitionWork(Commands &commands, const PartitionTarget &target) : SourceWork(commands, target.src), m_part(target.part)
    {
    }

    uint64_t Offset() const
    {
        return m_part.start * HDD_SECTOR_SIZE;
    }
    uint64_t Bytes() const
    {
        return m_part.sectors * HDD_SECTOR_SIZE;
    }
    PartitionTarget Target() const
    {
        return PartitionTarget{m_src, m_part};
    }

    // After a change: checks it again and logs the result
    void CheckAfter(FatxReader &reader, uint64_t sectors, FatxProgress &progress)
    {
        m_log.Add(TONE_TEXT, "  Checking the result:");
        auto after = std::make_unique<FatxCheckResult>();
        FatxCheck(reader, sectors * HDD_SECTOR_SIZE, after.get(), &progress);
        m_log.Check(*after);
    }

    const Part m_part;
};

} // namespace fatxgui

#endif // FATXGUI_PARTITION_WORK_HPP_

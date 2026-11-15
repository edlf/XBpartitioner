// See xbox_hdd_format.hpp

#include "xbox_hdd_format.hpp"

#include <string.h>

#include "fatx_format.hpp"
#include "pc_image.hpp" // FatxOffsetDevice

namespace
{

// Maps a step's 0 to 100 onto part of the overall progress
class StepProgress : public FatxProgress
{
  public:
    StepProgress(FatxProgress *whole, int from, int to) : m_whole(whole), m_from(from), m_to(to)
    {
    }
    void Update(int percent) override
    {
        if (m_whole != NULL)
        {
            m_whole->Update(m_from + (m_to - m_from) * percent / 100);
        }
    }

  private:
    FatxProgress *m_whole;
    int m_from;
    int m_to;
};

bool write_sector(FatxDevice &disk, uint64_t lba, const void *data)
{
    return disk.Write(lba * HDD_SECTOR_SIZE, data, HDD_SECTOR_SIZE) && disk.Sync();
}

// 1. No table: until the end, nothing reads it as an Xbox drive
bool erase_old_table(FatxDevice &disk)
{
    unsigned char zero[HDD_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    return write_sector(disk, 0, zero);
}

// 2. A new, empty FATX on each partition; up to 95% of the progress
bool format_partitions(FatxDevice &disk, const PartitionTableEditor &table, const XboxHddSpec &spec, FatxProgress *progress)
{
    const int count = table.GetActivePartitionCount();
    int done = 0;
    for (int i = 0; i < (int)XboxPartitionTableEntryCount; i++)
    {
        if (!table.IsPartitionEnabled(i))
        {
            continue;
        }
        const uint64_t sectors = table.GetPartitionSize(i);
        FatxOffsetDevice partition(disk, table.GetPartitionStart(i) * HDD_SECTOR_SIZE);
        StepProgress step(progress, done * 95 / count, (done + 1) * 95 / count);
        if (!FatxFormat(partition, sectors, ClusterSizeKbFor(sectors) * 1024, spec.serial + (uint32_t)i, &step))
        {
            return false;
        }
        done++;
    }
    return true;
}

// 3. The backups: in the last 4K block (where XBpartitioner looks on 512e
// drives) and the last sector (where it looks on the others)
bool write_backups(FatxDevice &disk, const PartitionTableEditor &table)
{
    const XboxPartitionTable data = table.GetPartitionTable();
    const uint64_t places[] = {table.GetBackupTableLba(), table.GetLastSectorLba()};
    for (const uint64_t lba : places)
    {
        // None on a stock sized drive: it would be in E
        if (table.GetBackupTableLba() != 0 && lba != 0 && !table.OverlapsSector(lba) && !write_sector(disk, lba, &data))
        {
            return false;
        }
    }
    return true;
}

} // namespace

PartitionTableEditor xbox_hdd_table(uint64_t sectors, const XboxHddSpec &spec)
{
    PartitionTableEditor table(sectors, spec.lba48_v4, true);
    switch (spec.layout)
    {
    case XBOX_HDD_F:
        table.SetupPartitionsFOnly();
        break;
    case XBOX_HDD_F_G:
        table.SetupPartitionsFGSplit();
        break;
    default:
        table.SetupPartitionsStock();
        break;
    }
    return table;
}

bool format_xbox_hdd(FatxDevice &disk, uint64_t disk_bytes, const XboxHddSpec &spec, FatxProgress *progress)
{
    if (disk_bytes < XBOX_HDD_MIN_BYTES)
    {
        return false;
    }
    const PartitionTableEditor table = xbox_hdd_table(disk_bytes / HDD_SECTOR_SIZE, spec);
    int first, second;
    if (table.FindConflict(&first, &second))
    {
        return false;
    }
    const XboxPartitionTable data = table.GetPartitionTable();
    // 4. Sector 0 last: from then on it's an Xbox drive
    const bool done = erase_old_table(disk) && format_partitions(disk, table, spec, progress) && write_backups(disk, table) && write_sector(disk, 0, &data);
    if (done && progress != NULL)
    {
        progress->Update(100);
    }
    return done;
}

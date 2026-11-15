#define NOMINMAX
#include "workspace.hpp"

#include <string.h>

#include "job_runner.hpp"

namespace fatxgui
{

namespace
{

uint32_t read_signature(FatxReader &reader)
{
    unsigned char sector[HDD_SECTOR_SIZE];
    uint32_t signature = 0;
    if (reader.Read(0, sector, sizeof(sector)))
    {
        memcpy(&signature, sector, 4);
    }
    return signature;
}

} // namespace

bool scan_source(Source *src, FatxProgress &whole, std::string *error)
{
    SourceIo io;
    if (!io.Open(*src, false, false, error))
    {
        return false;
    }
    src->bytes = io.Size();
    src->parts.clear();

    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    io.Disk().Read(0, &table, sizeof(table));
    src->has_table = PartitionTableEditor::HasMagic(table);
    if (src->has_table)
    {
        // The high bits only count from v4 on. 4K: keeps clear of the last 4K
        // block, where XBpartitioner puts the backup on 512e drives (and the
        // last sector, where it goes otherwise, is inside or past it).
        const PartitionTableEditor editor(table, src->bytes / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, true);
        for (int i = 0; i < XboxPartitionTableEntryCount; i++)
        {
            if (editor.IsPartitionEnabled(i))
            {
                Part part;
                part.index = i;
                part.start = editor.GetPartitionStart(i);
                part.sectors = editor.GetPartitionSize(i);
                src->parts.push_back(part);
            }
        }
        // Growing stops where XBpartitioner stops it: at the next partition,
        // or before the backup table at the end of the disk
        for (Part &part : src->parts)
        {
            part.max_sectors = part.sectors + editor.GetGrowthLimit(part.index);
        }
    }
    else if (src->type == Source::IMAGE)
    {
        Part part;
        part.start = 0;
        part.sectors = src->bytes / HDD_SECTOR_SIZE;
        part.max_sectors = (16ULL << 40) / HDD_SECTOR_SIZE; // The file just grows
        src->parts.push_back(part);
    }
    else
    {
        *error = "No Xbox partition table on this drive.";
        return false;
    }

    const int count = (int)src->parts.size();
    for (int i = 0; i < count; i++)
    {
        Part &part = src->parts[i];
        part.fits = (part.start + part.sectors) * HDD_SECTOR_SIZE <= src->bytes;
        if (!part.fits)
        {
            continue;
        }
        FatxOffsetReader reader(io.Disk(), part.start * HDD_SECTOR_SIZE);
        const uint32_t signature = read_signature(reader);
        part.resizing = FatxFindInterruptedResize(reader, &part.resize_old, &part.resize_new) || signature == FATX_RESIZING_SIGNATURE;
        part.fatx = signature == FATX_signature;
        if (part.fatx && !part.resizing)
        {
            SubProgress progress(whole, i * 100 / count, (i + 1) * 100 / count);
            part.usage_ok = FatxReadUsage(reader, part.sectors, &part.usage, &progress);
        }
    }

    if (src->type == Source::IMAGE)
    {
        std::vector<FatxRange> ranges;
        src->recluster_pending = load_recluster_plan(recluster_plan_path(src->path), &src->plan, &ranges);
    }
    return true;
}

} // namespace fatxgui

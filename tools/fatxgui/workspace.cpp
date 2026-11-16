#define NOMINMAX
#include "workspace.hpp"

#include <string.h>

#include "job_runner.hpp"
#include "stock_layout.hpp"

namespace fatxgui
{

namespace
{

// The enabled partitions in the table at sector 0; false if there is none
bool read_table_parts(FatxDevice &disk, uint64_t bytes, std::vector<Part> *parts)
{
    parts->clear();
    XboxPartitionTable table;
    memset(&table, 0, sizeof(table));
    if (!disk.Read(0, &table, sizeof(table)) || !PartitionTableEditor::HasMagic(table))
    {
        return false;
    }

    // The high bits only count from v4 on. 4K: keeps clear of the last 4K
    // block, where XBpartitioner puts the backup on 512e drives (and the
    // last sector, where it goes otherwise, is inside or past it).
    const PartitionTableEditor editor(table, bytes / HDD_SECTOR_SIZE, table.Version != XBOX_TABLE_VERSION_LBA48_V3, true);
    for (int i = 0; i < XboxPartitionTableEntryCount; i++)
    {
        if (editor.IsPartitionEnabled(i))
        {
            Part part;
            part.index = i;
            part.start = editor.GetPartitionStart(i);
            part.sectors = editor.GetPartitionSize(i);
            // Growing stops where XBpartitioner stops it: at the next
            // partition, or before the backup table at the end of the disk
            part.max_sectors = part.sectors + editor.GetGrowthLimit(i);
            parts->push_back(part);
        }
    }
    return true;
}

bool same_place(const Part &a, const Part &b)
{
    return a.index == b.index && a.start == b.start && a.sectors == b.sectors;
}

Part whole_volume_part(uint64_t sectors, uint64_t max_sectors)
{
    Part part;
    part.start = 0;
    part.sectors = sectors;
    part.max_sectors = max_sectors;
    return part;
}

} // namespace

bool layout_matches_scan(SourceIo &io, const Source &src)
{
    if (io.Size() != src.bytes)
    {
        return false;
    }
    std::vector<Part> parts;
    if (read_table_parts(io.Disk(), io.Size(), &parts) != src.has_table)
    {
        return false;
    }
    if (src.stock_layout)
    {
        return read_stock_parts(io.Disk(), &parts);
    }
    if (!src.has_table)
    {
        return true; // A partition image: its size is its layout
    }
    if (parts.size() != src.parts.size())
    {
        return false;
    }
    for (size_t i = 0; i < parts.size(); i++)
    {
        if (!same_place(parts[i], src.parts[i]))
        {
            return false;
        }
    }
    return true;
}

bool scan_source(Source *src, FatxProgress &whole, std::string *error)
{
    SourceIo io;
    if (!io.Open(*src, false, false, error))
    {
        return false;
    }
    src->bytes = io.Size();

    src->has_table = read_table_parts(io.Disk(), src->bytes, &src->parts);
    src->stock_layout = false;
    src->medium_needed = false;
    const uint64_t sectors = src->bytes / HDD_SECTOR_SIZE;
    const bool fatx_at_start = !src->has_table && has_fatx_at_start(io.Disk());
    if (src->has_table)
    {
        src->medium = FATX_MEDIUM_HARD_DRIVE;
    }
    else if (!fatx_at_start && read_stock_parts(io.Disk(), &src->parts))
    {
        src->stock_layout = true;
        src->medium = FATX_MEDIUM_HARD_DRIVE;
    }
    else if (src->type == Source::IMAGE)
    {
        src->parts.assign(1, whole_volume_part(sectors, (16ULL << 40) / HDD_SECTOR_SIZE)); // The file just grows
        // A memory unit or a hard drive partition: only the user knows
        if (src->medium == FATX_MEDIUM_UNKNOWN && fatx_at_start)
        {
            src->medium_needed = true;
            return true;
        }
    }
    else if (fatx_at_start)
    {
        // A memory unit, or a USB stick used as one: one volume, the whole drive
        src->medium = FATX_MEDIUM_MEMORY_UNIT;
        src->parts.assign(1, whole_volume_part(sectors, sectors));
    }
    else
    {
        *error = "Neither an Xbox partition table nor FATX on this drive.";
        return false;
    }
    io.UseMedium(src->medium);

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

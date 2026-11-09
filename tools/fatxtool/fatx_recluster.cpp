#include "fatx_recluster.hpp"
#include "fatx_resize.hpp" // FATX_RESIZING_SIGNATURE
#include "fatx_writer.hpp"
#include <string.h>

bool FatxRangeRecorder::Read(uint64_t offset, void *buffer, uint32_t length)
{
    return m_inner.Read(offset, buffer, length);
}

bool FatxRangeRecorder::Write(uint64_t offset, const void *buffer, uint32_t length)
{
    if (!m_inner.Write(offset, buffer, length))
    {
        return false;
    }

    // Merge with neighbouring and overlapping ranges
    uint64_t begin = offset;
    uint64_t end = offset + length;
    auto next = m_ranges.upper_bound(begin);
    if (next != m_ranges.begin())
    {
        auto previous = std::prev(next);
        if (previous->second >= begin)
        {
            begin = previous->first;
            end = (previous->second > end) ? previous->second : end;
            next = m_ranges.erase(previous);
        }
    }
    while (next != m_ranges.end() && next->first <= end)
    {
        end = (next->second > end) ? next->second : end;
        next = m_ranges.erase(next);
    }
    m_ranges[begin] = end;
    return true;
}

std::vector<FatxRange> FatxRangeRecorder::Ranges() const
{
    std::vector<FatxRange> ranges;
    for (const auto &range : m_ranges)
    {
        FatxRange item;
        item.offset = range.first;
        item.length = range.second - range.first;
        ranges.push_back(item);
    }
    return ranges;
}

namespace
{

// Reads a file's contents from the original
class ChainSource : public FatxSource
{
  public:
    ChainSource(FatxWriter &original, FatxDevice &device, uint32_t first)
        : m_original(original), m_device(device), m_next(first), m_data(original.ClusterBytes()), m_position(original.ClusterBytes())
    {
    }

    bool Read(void *buffer, uint32_t length) override
    {
        unsigned char *out = (unsigned char *)buffer;
        while (length > 0)
        {
            if (m_position == m_data.size())
            {
                if (m_next < 1 || m_next >= m_original.Layout().cluster_limit ||
                    !m_device.Read(m_original.ClusterOffset(m_next), m_data.data(), (uint32_t)m_data.size()))
                {
                    return false;
                }
                m_next = m_original.GetFat(m_next);
                m_position = 0;
            }
            const uint32_t part = (uint32_t)((length < m_data.size() - m_position) ? length : m_data.size() - m_position);
            memcpy(out, m_data.data() + m_position, part);
            out += part;
            m_position += part;
            length -= part;
        }
        return true;
    }

  private:
    FatxWriter &m_original;
    FatxDevice &m_device;
    uint32_t m_next;
    std::vector<unsigned char> m_data;
    size_t m_position;
};

// Reads a folder's live entries in order
bool read_folder(FatxWriter &original, FatxDevice &device, uint32_t first, std::vector<FatxDirent> *entries)
{
    const uint32_t per_cluster = original.ClusterBytes() / sizeof(FatxDirent);
    std::vector<unsigned char> data(original.ClusterBytes());
    for (uint32_t cluster = first; cluster >= 1 && cluster < original.Layout().cluster_limit; cluster = original.GetFat(cluster))
    {
        if (!device.Read(original.ClusterOffset(cluster), data.data(), (uint32_t)data.size()))
        {
            return false;
        }
        for (uint32_t i = 0; i < per_cluster; i++)
        {
            FatxDirent entry;
            memcpy(&entry, data.data() + i * sizeof(FatxDirent), sizeof(entry));
            if (entry.name_length == FATX_DIRENT_END || entry.name_length == FATX_DIRENT_END2)
            {
                return true;
            }
            if (entry.name_length != FATX_DIRENT_DELETED && entry.name_length <= FATX_MAX_NAME_LENGTH)
            {
                entries->push_back(entry);
            }
        }
    }
    return true;
}

typedef struct
{
    uint32_t first_cluster; // In the original's numbering
    std::vector<FatxDirent> entries;
} Folder;

class PartProgress : public FatxProgress
{
  public:
    PartProgress(FatxProgress *whole, int from, int to) : m_whole(whole), m_from(from), m_to(to)
    {
    }
    void Update(int percent) override
    {
        if (m_whole != NULL)
        {
            m_whole->Update(m_from + percent * (m_to - m_from) / 100);
        }
    }

  private:
    FatxProgress *m_whole;
    int m_from;
    int m_to;
};

bool fail(FatxReclusterResult *result, FatxReclusterStatus status)
{
    result->status = status;
    return false;
}

} // namespace

bool FatxBuildReclustered(FatxDevice &original, uint64_t lba_size, uint32_t new_cluster_bytes, FatxRangeRecorder *build, FatxReclusterResult *result,
                          FatxProgress *progress)
{
    memset(result, 0, sizeof(*result));
    result->new_cluster_bytes = new_cluster_bytes;

    unsigned char header[FATX_HEADER_BYTES];
    if (!original.Read(0, header, sizeof(header)))
    {
        return fail(result, FATX_RECLUSTER_READ_ERROR);
    }
    uint32_t signature, sectors_per_cluster;
    memcpy(&signature, header + FATX_SIGNATURE_OFFSET, 4);
    memcpy(&sectors_per_cluster, header + FATX_SECTORS_PER_CLUSTER_OFFSET, 4);
    if (signature == FATX_RESIZING_SIGNATURE)
    {
        return fail(result, FATX_RECLUSTER_RESIZING);
    }
    if (signature != FATX_signature)
    {
        return fail(result, FATX_RECLUSTER_NOT_FATX);
    }
    result->old_cluster_bytes = sectors_per_cluster * HDD_SECTOR_SIZE;

    if (new_cluster_bytes < 16 * 1024 || new_cluster_bytes > 1024 * 1024 || (new_cluster_bytes & (new_cluster_bytes - 1)) != 0)
    {
        return fail(result, FATX_RECLUSTER_BAD_SIZE);
    }
    if (new_cluster_bytes == result->old_cluster_bytes)
    {
        return fail(result, FATX_RECLUSTER_SAME);
    }
    if (new_cluster_bytes / 1024 < ClusterSizeKbFor(lba_size))
    {
        return fail(result, FATX_RECLUSTER_TOO_SMALL);
    }

    // Warnings are fine: lost and extra clusters get dropped
    PartProgress check_progress(progress, 0, 20);
    FatxCheck(original, lba_size * HDD_SECTOR_SIZE, &result->check, &check_progress);
    if (result->check.status != FATX_CHECK_DONE)
    {
        return fail(result, FATX_RECLUSTER_READ_ERROR);
    }
    if (result->check.errors > 0)
    {
        return fail(result, FATX_RECLUSTER_HAS_ERRORS);
    }
    result->old_clusters_used = result->check.used_clusters;

    FatxWriter source(original, lba_size * HDD_SECTOR_SIZE);
    if (!source.Load())
    {
        return fail(result, FATX_RECLUSTER_READ_ERROR);
    }

    // Read every folder, root first, and count the clusters needed
    std::vector<Folder> folders(1);
    folders[0].first_cluster = source.RootCluster();
    const uint64_t per_cluster = new_cluster_bytes / sizeof(FatxDirent);
    uint64_t bytes_to_copy = 0;
    for (size_t f = 0; f < folders.size(); f++)
    {
        std::vector<FatxDirent> entries;
        if (!read_folder(source, original, folders[f].first_cluster, &entries))
        {
            return fail(result, FATX_RECLUSTER_READ_ERROR);
        }
        result->clusters_needed += entries.empty() ? 1 : (entries.size() + per_cluster - 1) / per_cluster;
        for (const FatxDirent &entry : entries)
        {
            if (entry.attributes & FATX_ATTR_DIRECTORY)
            {
                result->folders++;
                Folder folder;
                folder.first_cluster = entry.first_cluster;
                folders.push_back(folder);
            }
            else
            {
                result->files++;
                result->clusters_needed += ((uint64_t)entry.file_size + new_cluster_bytes - 1) / new_cluster_bytes;
                bytes_to_copy += entry.file_size;
            }
        }
        folders[f].entries.swap(entries);
    }

    const FatxLayout layout = FatxLayoutFor(lba_size, new_cluster_bytes);
    result->clusters_available = (layout.cluster_limit > 1) ? layout.cluster_limit - 1 : 0;
    if (result->clusters_needed > result->clusters_available)
    {
        return fail(result, FATX_RECLUSTER_NO_ROOM);
    }
    if (build == NULL)
    {
        result->status = FATX_RECLUSTER_PLANNED;
        return true;
    }

    // Build the new file system in the same order
    uint32_t serial;
    memcpy(&serial, header + 4, 4);
    FatxWriter target(*build, lba_size * HDD_SECTOR_SIZE, new_cluster_bytes);
    if (!target.Format(serial))
    {
        return fail(result, FATX_RECLUSTER_WRITE_ERROR);
    }

    // folders[f] becomes new_clusters[f]; subfolders always come later in the list
    std::vector<uint32_t> new_clusters(folders.size(), 0);
    new_clusters[0] = target.RootCluster();
    size_t next_folder = 1;
    uint64_t copied = 0;
    for (size_t f = 0; f < folders.size(); f++)
    {
        for (const FatxDirent &entry : folders[f].entries)
        {
            if (entry.attributes & FATX_ATTR_DIRECTORY)
            {
                if (!target.MakeDirectory(new_clusters[f], entry, &new_clusters[next_folder++]))
                {
                    return fail(result, FATX_RECLUSTER_WRITE_ERROR);
                }
                continue;
            }
            ChainSource contents(source, original, entry.first_cluster);
            if (!target.AddFile(new_clusters[f], entry, contents))
            {
                return fail(result, FATX_RECLUSTER_WRITE_ERROR);
            }
            copied += entry.file_size;
            if (progress != NULL && bytes_to_copy > 0)
            {
                progress->Update(20 + (int)(copied * 70 / bytes_to_copy));
            }
        }
    }
    if (!target.Flush())
    {
        return fail(result, FATX_RECLUSTER_WRITE_ERROR);
    }

    // Keep the original header (serial number, volume name), with the new
    // cluster size and root
    const uint32_t new_sectors = new_cluster_bytes / HDD_SECTOR_SIZE;
    const uint32_t root = target.RootCluster();
    memcpy(header + FATX_SECTORS_PER_CLUSTER_OFFSET, &new_sectors, 4);
    memcpy(header + FATX_ROOT_CLUSTER_OFFSET, &root, 4);
    if (!build->Write(0, header, sizeof(header)) || !build->Sync())
    {
        return fail(result, FATX_RECLUSTER_WRITE_ERROR);
    }

    // Must check clean before touching the partition
    FatxCheckResult *built = new FatxCheckResult;
    PartProgress verify_progress(progress, 90, 100);
    FatxCheck(*build, lba_size * HDD_SECTOR_SIZE, built, &verify_progress);
    const bool clean = built->status == FATX_CHECK_DONE && built->errors == 0 && built->lost_clusters == 0 && built->files == result->files &&
                       built->directories == result->folders;
    delete built;
    if (!clean)
    {
        return fail(result, FATX_RECLUSTER_BUILD_FAILED);
    }
    result->status = FATX_RECLUSTER_DONE;
    return true;
}

bool FatxCopyReclustered(FatxDevice &build, FatxDevice &partition, const std::vector<FatxRange> &ranges, FatxProgress *progress)
{
    // Block mounting until the header is copied, last
    unsigned char sector[HDD_SECTOR_SIZE];
    const uint32_t resizing = FATX_RESIZING_SIGNATURE;
    if (!partition.Read(0, sector, sizeof(sector)))
    {
        return false;
    }
    memcpy(sector + FATX_SIGNATURE_OFFSET, &resizing, 4);
    if (!partition.Write(0, sector, sizeof(sector)) || !partition.Sync())
    {
        return false;
    }

    uint64_t total = 0;
    for (const FatxRange &range : ranges)
    {
        total += range.length;
    }
    const uint32_t chunk = 1024 * 1024;
    std::vector<unsigned char> data(chunk);
    uint64_t done = 0;
    for (const FatxRange &range : ranges)
    {
        uint64_t offset = range.offset;
        const uint64_t end = range.offset + range.length;
        if (offset < HDD_SECTOR_SIZE)
        {
            offset = HDD_SECTOR_SIZE; // Sector 0 goes last
        }
        while (offset < end)
        {
            const uint32_t length = (uint32_t)((end - offset < chunk) ? end - offset : chunk);
            if (!build.Read(offset, data.data(), length) || !partition.Write(offset, data.data(), length))
            {
                return false;
            }
            offset += length;
            done += length;
            if (progress != NULL && total > 0)
            {
                progress->Update((int)(done * 99 / total));
            }
        }
    }
    if (!partition.Sync() || !build.Read(0, sector, sizeof(sector)) || !partition.Write(0, sector, sizeof(sector)) || !partition.Sync())
    {
        return false;
    }
    if (progress != NULL)
    {
        progress->Update(100);
    }
    return true;
}

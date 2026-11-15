#include "fatx_folder.hpp"
#include "fatx_resize.hpp" // FATX_RESIZING_SIGNATURE
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <unordered_set>

namespace
{

// FAT read size; a multiple of 4 KB, like the check's
const uint32_t FAT_CHUNK_BYTES = 0x10000;

// Consecutive clusters of a file are read together, up to this much
const uint32_t READ_RUN_BYTES = 1024 * 1024;

// The UTF-16 volume name from offset 16, up to a 0 or 0xFFFF
std::string label_from(const unsigned char *header)
{
    std::string label;
    for (uint32_t i = 0; i < 32; i++)
    {
        uint16_t c;
        memcpy(&c, header + 16 + i * 2, 2);
        if (c == 0 || c == 0xFFFF)
        {
            break;
        }
        label += (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    return label;
}

std::string printable_name(const FatxDirent &entry)
{
    std::string name;
    for (unsigned int i = 0; i < entry.name_length; i++)
    {
        const unsigned char c = (unsigned char)entry.name[i];
        name += (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    return name;
}

} // namespace

FatxFolderReader::FatxFolderReader(FatxReader &reader, uint64_t partition_bytes)
    : m_reader(reader), m_partition_bytes(partition_bytes), m_cluster_bytes(0), m_root(0), m_fat_chunk(FAT_CHUNK_BYTES), m_fat_chunk_offset(0),
      m_fat_chunk_valid(false)
{
    memset(&m_layout, 0, sizeof(m_layout));
}

FatxFolderStatus FatxFolderReader::Open()
{
    unsigned char header[HDD_SECTOR_SIZE];
    if (m_partition_bytes < FATX_HEADER_BYTES || !m_reader.Read(0, header, sizeof(header)))
    {
        return FATX_FOLDER_READ_ERROR;
    }

    uint32_t signature, sectors_per_cluster, root;
    memcpy(&signature, header + FATX_SIGNATURE_OFFSET, 4);
    memcpy(&sectors_per_cluster, header + FATX_SECTORS_PER_CLUSTER_OFFSET, 4);
    memcpy(&root, header + FATX_ROOT_CLUSTER_OFFSET, 4);
    if (signature == FATX_RESIZING_SIGNATURE)
    {
        return FATX_FOLDER_RESIZING;
    }
    if (signature != FATX_signature)
    {
        return FATX_FOLDER_NOT_FATX;
    }
    m_cluster_bytes = FatxClusterBytes(sectors_per_cluster, m_reader.Medium());
    if (m_cluster_bytes == 0)
    {
        return FATX_FOLDER_BAD_HEADER;
    }
    m_layout = FatxLayoutFor(m_partition_bytes / HDD_SECTOR_SIZE, m_cluster_bytes);
    if (m_layout.cluster_limit < 2 || m_layout.cluster_limit > FATX_FAT_RESERVED || root < 1 || root >= m_layout.cluster_limit)
    {
        return FATX_FOLDER_BAD_HEADER;
    }
    m_root = root;
    m_label = label_from(header);
    return FATX_FOLDER_OK;
}

FatxFolderStatus FatxFolderReader::List(uint32_t first_cluster, std::vector<FatxFolderEntry> *entries)
{
    entries->clear();
    std::vector<unsigned char> cluster_data(m_cluster_bytes);
    std::unordered_set<uint32_t> visited; // A cluster seen twice is a loop
    uint32_t cluster = first_cluster;
    bool ended = false;

    while (!ended)
    {
        if (cluster < 1 || cluster >= m_layout.cluster_limit || !visited.insert(cluster).second)
        {
            return FATX_FOLDER_BROKEN_CHAIN;
        }
        const uint64_t offset = m_layout.data_offset + (uint64_t)(cluster - 1) * m_cluster_bytes;
        if (!m_reader.Read(offset, cluster_data.data(), m_cluster_bytes))
        {
            return FATX_FOLDER_READ_ERROR;
        }
        AddEntries(cluster_data, offset, entries, &ended);
        if (ended)
        {
            break;
        }

        uint32_t next;
        if (!NextCluster(cluster, &next))
        {
            return FATX_FOLDER_READ_ERROR;
        }
        if (next == FATX_FAT_END)
        {
            break; // A full last cluster needs no end marker
        }
        if (next == FATX_FAT_FREE || next >= FATX_FAT_RESERVED)
        {
            return FATX_FOLDER_BROKEN_CHAIN;
        }
        cluster = next;
    }
    return FATX_FOLDER_OK;
}

FatxFolderStatus FatxFolderReader::ReadFile(const FatxFolderEntry &file, FatxFileSink &sink)
{
    const uint64_t needed = ((uint64_t)file.size + m_cluster_bytes - 1) / m_cluster_bytes;
    const uint32_t most_per_read = std::max<uint32_t>(1, READ_RUN_BYTES / m_cluster_bytes);
    std::vector<unsigned char> buffer((size_t)std::min<uint64_t>(needed, most_per_read) * m_cluster_bytes);
    uint64_t left = file.size;
    uint64_t clusters_done = 0;
    uint32_t cluster = file.first_cluster;

    while (clusters_done < needed)
    {
        const uint32_t most = (uint32_t)std::min<uint64_t>(needed - clusters_done, most_per_read);
        uint32_t run;
        uint32_t after;
        const FatxFolderStatus status = FindRun(cluster, most, &run, &after);
        if (status != FATX_FOLDER_OK)
        {
            return status;
        }

        const uint64_t run_bytes = (uint64_t)run * m_cluster_bytes;
        const uint32_t length = (uint32_t)std::min<uint64_t>(left, run_bytes);
        const uint64_t offset = m_layout.data_offset + (uint64_t)(cluster - 1) * m_cluster_bytes;
        if (!m_reader.Read(offset, buffer.data(), (uint32_t)run_bytes) || !sink.Write(buffer.data(), length))
        {
            return FATX_FOLDER_READ_ERROR;
        }
        left -= length;
        clusters_done += run;
        cluster = after;
    }
    return FATX_FOLDER_OK;
}

FatxFolderStatus FatxFolderReader::FindRun(uint32_t first, uint32_t most, uint32_t *length, uint32_t *after)
{
    *length = 0;
    uint32_t cluster = first;
    while (true)
    {
        if (cluster < 1 || cluster >= m_layout.cluster_limit)
        {
            return FATX_FOLDER_BROKEN_CHAIN;
        }
        (*length)++;
        uint32_t next;
        if (!NextCluster(cluster, &next))
        {
            return FATX_FOLDER_READ_ERROR;
        }
        *after = next;
        if (*length == most || next != cluster + 1)
        {
            break;
        }
        cluster = next;
    }
    // If more of the file follows, the next call checks *after is a cluster:
    // free (0) and the markers are all outside the partition's range
    return FATX_FOLDER_OK;
}

void FatxFolderReader::AddEntries(const std::vector<unsigned char> &cluster, uint64_t cluster_offset, std::vector<FatxFolderEntry> *entries, bool *ended) const
{
    const uint32_t count = m_cluster_bytes / sizeof(FatxDirent);
    for (uint32_t i = 0; i < count; i++)
    {
        FatxDirent dirent;
        memcpy(&dirent, cluster.data() + i * sizeof(FatxDirent), sizeof(dirent));
        if (dirent.name_length == FATX_DIRENT_END || dirent.name_length == FATX_DIRENT_END2)
        {
            *ended = true;
            return;
        }
        if (dirent.name_length == FATX_DIRENT_DELETED || dirent.name_length > FATX_MAX_NAME_LENGTH)
        {
            continue;
        }

        FatxFolderEntry entry;
        entry.name = printable_name(dirent);
        entry.directory = (dirent.attributes & FATX_ATTR_DIRECTORY) != 0;
        entry.first_cluster = dirent.first_cluster;
        entry.size = entry.directory ? 0 : dirent.file_size;
        entry.modified_date = dirent.modified_date;
        entry.modified_time = dirent.modified_time;
        entry.dirent_offset = cluster_offset + i * sizeof(FatxDirent);
        entries->push_back(entry);
    }
}

bool FatxFolderReader::NextCluster(uint32_t cluster, uint32_t *next)
{
    const uint64_t byte = (uint64_t)cluster * m_layout.bytes_per_entry;
    const uint64_t chunk_offset = byte - byte % FAT_CHUNK_BYTES;
    if (!m_fat_chunk_valid || chunk_offset != m_fat_chunk_offset)
    {
        // The FAT is whole 4 KB pages, so the last chunk is too
        const uint64_t left = m_layout.bytes_per_fat - chunk_offset;
        const uint32_t length = (uint32_t)(left < FAT_CHUNK_BYTES ? left : FAT_CHUNK_BYTES);
        m_fat_chunk_valid = m_reader.Read(FATX_HEADER_BYTES + chunk_offset, m_fat_chunk.data(), length);
        if (!m_fat_chunk_valid)
        {
            return false;
        }
        m_fat_chunk_offset = chunk_offset;
    }

    const unsigned char *entry = m_fat_chunk.data() + (byte - chunk_offset);
    if (m_layout.bytes_per_entry == 2)
    {
        uint16_t value16;
        memcpy(&value16, entry, 2);
        *next = (value16 >= 0xFFF0) ? (0xFFFF0000u | value16) : value16;
    }
    else
    {
        memcpy(next, entry, 4);
    }
    return true;
}

namespace
{

// CON, PRN, AUX, NUL, COM1-9 and LPT1-9, also with an extension
bool is_device_name(const std::string &name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL"};
    const std::string base = name.substr(0, name.find('.'));
    for (const char *device : devices)
    {
        if (_stricmp(base.c_str(), device) == 0)
        {
            return true;
        }
    }
    return base.size() == 4 && (_strnicmp(base.c_str(), "COM", 3) == 0 || _strnicmp(base.c_str(), "LPT", 3) == 0) && base[3] >= '1' && base[3] <= '9';
}

} // namespace

std::string windows_file_name(const std::string &fatx_name)
{
    std::string name = fatx_name;
    for (char &c : name)
    {
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || strchr("\"*/:<>?\\|", u) != NULL)
        {
            c = '_';
        }
    }
    if (!name.empty() && (name.back() == '.' || name.back() == ' '))
    {
        name.back() = '_';
    }
    if (name.empty() || is_device_name(name))
    {
        name = "_" + name;
    }
    return name;
}

std::string fatx_time_text(uint16_t date, uint16_t time)
{
    const unsigned int day = date & 0x1F;
    const unsigned int month = (date >> 5) & 0xF;
    const unsigned int year = 2000 + (date >> 9);
    if (day == 0 || month == 0 || month > 12)
    {
        return std::string();
    }
    char text[32];
    snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", year, month, day, (unsigned int)(time >> 11), (unsigned int)((time >> 5) & 0x3F));
    return text;
}

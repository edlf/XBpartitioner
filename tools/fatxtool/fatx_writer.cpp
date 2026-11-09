#include "fatx_writer.hpp"
#include <string.h>

bool FatxMemoryDevice::Read(uint64_t offset, void *buffer, uint32_t length)
{
    if (offset > m_data.size() || length > m_data.size() - offset)
    {
        return false;
    }
    memcpy(buffer, m_data.data() + offset, length);
    return true;
}

bool FatxMemoryDevice::Write(uint64_t offset, const void *buffer, uint32_t length)
{
    if (offset > m_data.size() || length > m_data.size() - offset)
    {
        return false;
    }
    memcpy(m_data.data() + offset, buffer, length);
    return true;
}

namespace
{

class MemorySource : public FatxSource
{
  public:
    MemorySource(const void *data, uint32_t size) : m_data((const unsigned char *)data), m_left(size)
    {
    }
    bool Read(void *buffer, uint32_t length) override
    {
        if (length > m_left)
        {
            return false;
        }
        memcpy(buffer, m_data, length);
        m_data += length;
        m_left -= length;
        return true;
    }

  private:
    const unsigned char *m_data;
    uint32_t m_left;
};

// A fixed timestamp: 2001-11-15 12:00
const uint16_t ENTRY_DATE = (15 & 0x1F) | ((11 & 0xF) << 5) | (((2001 - 2000) & 0x7F) << 9);
const uint16_t ENTRY_TIME = (12 & 0xF) << 11;

} // namespace

FatxWriter::FatxWriter(FatxDevice &device, uint64_t partition_bytes, uint32_t cluster_bytes)
    : m_device(device), m_partition_bytes(partition_bytes), m_cluster_bytes(cluster_bytes), m_root(1), m_next_free(1)
{
    if (m_cluster_bytes == 0)
    {
        const XboxPartitionTableManipulator sizes(0, true, false);
        m_cluster_bytes = sizes.CalculateClusterSize(partition_bytes / HDD_SECTOR_SIZE) * 1024;
    }
    m_layout = FatxLayoutFor(partition_bytes / HDD_SECTOR_SIZE, m_cluster_bytes);
}

uint64_t FatxWriter::ClusterOffset(uint32_t cluster) const
{
    return m_layout.data_offset + (uint64_t)(cluster - 1) * m_cluster_bytes;
}

bool FatxWriter::Format(uint32_t serial)
{
    if (m_layout.cluster_limit < 2)
    {
        return false;
    }

    // Header fields; the rest is 0xFF
    std::vector<unsigned char> header(FATX_HEADER_BYTES, 0xFF);
    const uint32_t signature = FATX_signature;
    const uint32_t sectors_per_cluster = m_cluster_bytes / HDD_SECTOR_SIZE;
    const uint16_t unknown = 0;
    memcpy(&header[FATX_SIGNATURE_OFFSET], &signature, 4);
    memcpy(&header[4], &serial, 4);
    memcpy(&header[FATX_SECTORS_PER_CLUSTER_OFFSET], &sectors_per_cluster, 4);
    memcpy(&header[FATX_ROOT_CLUSTER_OFFSET], &m_root, 4);
    memcpy(&header[16], &unknown, 2);
    if (!m_device.Write(0, header.data(), (uint32_t)header.size()))
    {
        return false;
    }

    m_fat.assign((size_t)m_layout.cluster_limit, FATX_FAT_FREE);
    m_fat[0] = FATX_FAT_MEDIA;
    m_fat[m_root] = FATX_FAT_END;
    m_next_free = m_root + 1;
    return FillCluster(m_root, FATX_DIRENT_END) && Flush();
}

bool FatxWriter::Load()
{
    unsigned char header[HDD_SECTOR_SIZE];
    if (!m_device.Read(0, header, sizeof(header)))
    {
        return false;
    }
    uint32_t signature, sectors_per_cluster;
    memcpy(&signature, header + FATX_SIGNATURE_OFFSET, 4);
    memcpy(&sectors_per_cluster, header + FATX_SECTORS_PER_CLUSTER_OFFSET, 4);
    memcpy(&m_root, header + FATX_ROOT_CLUSTER_OFFSET, 4);
    if (signature != FATX_signature || sectors_per_cluster == 0)
    {
        return false;
    }
    m_cluster_bytes = sectors_per_cluster * HDD_SECTOR_SIZE;
    m_layout = FatxLayoutFor(m_partition_bytes / HDD_SECTOR_SIZE, m_cluster_bytes);

    std::vector<unsigned char> raw((size_t)m_layout.bytes_per_fat);
    if (!m_device.Read(FATX_HEADER_BYTES, raw.data(), (uint32_t)raw.size()))
    {
        return false;
    }
    m_fat.assign((size_t)m_layout.cluster_limit, FATX_FAT_FREE);
    for (size_t i = 0; i < m_fat.size(); i++)
    {
        if (m_layout.bytes_per_entry == 2)
        {
            uint16_t value;
            memcpy(&value, &raw[i * 2], 2);
            m_fat[i] = (value >= 0xFFF0) ? (0xFFFF0000u | value) : value;
        }
        else
        {
            memcpy(&m_fat[i], &raw[i * 4], 4);
        }
    }
    m_next_free = 1;
    return true;
}

bool FatxWriter::Flush()
{
    std::vector<unsigned char> raw((size_t)m_layout.bytes_per_fat, 0);
    for (size_t i = 0; i < m_fat.size(); i++)
    {
        if (m_layout.bytes_per_entry == 2)
        {
            const uint16_t value = (uint16_t)m_fat[i];
            memcpy(&raw[i * 2], &value, 2);
        }
        else
        {
            memcpy(&raw[i * 4], &m_fat[i], 4);
        }
    }
    return m_device.Write(FATX_HEADER_BYTES, raw.data(), (uint32_t)raw.size());
}

uint32_t FatxWriter::GetFat(uint32_t cluster) const
{
    return cluster < m_fat.size() ? m_fat[cluster] : FATX_FAT_FREE;
}

void FatxWriter::SetFat(uint32_t cluster, uint32_t value)
{
    if (cluster < m_fat.size())
    {
        m_fat[cluster] = value;
    }
}

bool FatxWriter::FillCluster(uint32_t cluster, unsigned char value)
{
    std::vector<unsigned char> data(m_cluster_bytes, value);
    return m_device.Write(ClusterOffset(cluster), data.data(), m_cluster_bytes);
}

bool FatxWriter::AllocateChain(uint32_t count, uint32_t *first)
{
    const uint32_t limit = (uint32_t)m_layout.cluster_limit;
    uint32_t previous = 0;
    *first = 0;
    for (uint32_t found = 0; found < count; found++)
    {
        uint32_t cluster = m_next_free;
        uint32_t tried = 0;
        while (m_fat[cluster] != FATX_FAT_FREE)
        {
            cluster = (cluster + 1 < limit) ? cluster + 1 : 1;
            if (++tried >= limit)
            {
                return false; // Full
            }
        }
        m_fat[cluster] = FATX_FAT_END;
        if (previous != 0)
        {
            m_fat[previous] = cluster;
        }
        else
        {
            *first = cluster;
        }
        previous = cluster;
        m_next_free = (cluster + 1 < limit) ? cluster + 1 : 1;
    }
    return true;
}

bool FatxWriter::MakeEntry(const char *name, unsigned char attributes, uint32_t first, uint32_t size, FatxDirent *entry)
{
    const size_t length = strlen(name);
    if (length == 0 || length > FATX_MAX_NAME_LENGTH)
    {
        return false;
    }
    memset(entry, 0xFF, sizeof(*entry)); // Unused name bytes are 0xFF
    entry->name_length = (unsigned char)length;
    entry->attributes = attributes;
    memcpy(entry->name, name, length);
    entry->first_cluster = first;
    entry->file_size = size;
    entry->modified_date = entry->created_date = entry->accessed_date = ENTRY_DATE;
    entry->modified_time = entry->created_time = entry->accessed_time = ENTRY_TIME;
    return true;
}

bool FatxWriter::AddEntry(uint32_t parent, const FatxDirent &entry)
{
    std::vector<unsigned char> data(m_cluster_bytes);
    const uint32_t per_cluster = m_cluster_bytes / sizeof(FatxDirent);
    uint32_t cluster = parent;
    while (true)
    {
        if (!m_device.Read(ClusterOffset(cluster), data.data(), m_cluster_bytes))
        {
            return false;
        }
        for (uint32_t i = 0; i < per_cluster; i++)
        {
            const unsigned char length = data[i * sizeof(FatxDirent)];
            if (length == FATX_DIRENT_END || length == FATX_DIRENT_END2 || length == FATX_DIRENT_DELETED)
            {
                return m_device.Write(ClusterOffset(cluster) + i * sizeof(FatxDirent), &entry, sizeof(entry));
            }
        }
        const uint32_t next = m_fat[cluster];
        if (next == FATX_FAT_END || next == FATX_FAT_FREE || next >= m_fat.size())
        {
            break;
        }
        cluster = next;
    }

    // The folder is full; add a cluster
    uint32_t added;
    if (!AllocateChain(1, &added) || !FillCluster(added, FATX_DIRENT_END))
    {
        return false;
    }
    m_fat[cluster] = added;
    return m_device.Write(ClusterOffset(added), &entry, sizeof(entry));
}

bool FatxWriter::MakeDirectory(uint32_t parent, const char *name, uint32_t *cluster)
{
    FatxDirent entry;
    uint32_t first;
    if (!MakeEntry(name, 0, 0, 0, &entry) || !AllocateChain(1, &first) || !FillCluster(first, FATX_DIRENT_END))
    {
        return false;
    }
    entry.attributes = FATX_ATTR_DIRECTORY;
    entry.first_cluster = first;
    if (cluster != NULL)
    {
        *cluster = first;
    }
    return AddEntry(parent, entry);
}

bool FatxWriter::WriteChain(uint32_t size, FatxSource &source, uint32_t *first)
{
    const uint32_t count = (uint32_t)(((uint64_t)size + m_cluster_bytes - 1) / m_cluster_bytes);
    *first = 0;
    if (count > 0 && !AllocateChain(count, first))
    {
        return false;
    }

    std::vector<unsigned char> data(m_cluster_bytes);
    uint32_t left = size;
    for (uint32_t cluster = *first; left > 0; cluster = m_fat[cluster])
    {
        const uint32_t length = left < m_cluster_bytes ? left : m_cluster_bytes;
        memset(data.data(), 0, m_cluster_bytes);
        if (!source.Read(data.data(), length) || !m_device.Write(ClusterOffset(cluster), data.data(), m_cluster_bytes))
        {
            return false;
        }
        left -= length;
    }
    return true;
}

bool FatxWriter::AddFile(uint32_t parent, const char *name, uint32_t size, FatxSource &source, uint32_t *first_cluster)
{
    FatxDirent entry;
    uint32_t first;
    if (!MakeEntry(name, 0, 0, size, &entry) || !WriteChain(size, source, &first))
    {
        return false;
    }
    entry.first_cluster = first;
    if (first_cluster != NULL)
    {
        *first_cluster = first;
    }
    return AddEntry(parent, entry);
}

bool FatxWriter::AddFile(uint32_t parent, const FatxDirent &like, FatxSource &source, uint32_t *first_cluster)
{
    uint32_t first;
    if (like.name_length == 0 || like.name_length > FATX_MAX_NAME_LENGTH || !WriteChain(like.file_size, source, &first))
    {
        return false;
    }
    FatxDirent entry = like;
    entry.attributes &= (unsigned char)~FATX_ATTR_DIRECTORY;
    entry.first_cluster = first;
    if (first_cluster != NULL)
    {
        *first_cluster = first;
    }
    return AddEntry(parent, entry);
}

bool FatxWriter::MakeDirectory(uint32_t parent, const FatxDirent &like, uint32_t *cluster)
{
    uint32_t first;
    if (like.name_length == 0 || like.name_length > FATX_MAX_NAME_LENGTH || !AllocateChain(1, &first) || !FillCluster(first, FATX_DIRENT_END))
    {
        return false;
    }
    FatxDirent entry = like;
    entry.attributes |= FATX_ATTR_DIRECTORY;
    entry.first_cluster = first;
    entry.file_size = 0;
    if (cluster != NULL)
    {
        *cluster = first;
    }
    return AddEntry(parent, entry);
}

bool FatxWriter::AddFile(uint32_t parent, const char *name, const void *data, uint32_t size, uint32_t *first_cluster)
{
    MemorySource source(data, size);
    return AddFile(parent, name, size, source, first_cluster);
}

bool FatxWriter::GetEntry(uint32_t dir, uint32_t index, FatxDirent *entry, uint64_t *offset)
{
    const uint32_t per_cluster = m_cluster_bytes / sizeof(FatxDirent);
    uint32_t seen = 0;
    for (uint32_t cluster = dir; cluster >= 1 && cluster < m_fat.size(); cluster = m_fat[cluster])
    {
        for (uint32_t i = 0; i < per_cluster; i++)
        {
            const uint64_t at = ClusterOffset(cluster) + i * sizeof(FatxDirent);
            if (!m_device.Read(at, entry, sizeof(*entry)))
            {
                return false;
            }
            if (entry->name_length == FATX_DIRENT_END || entry->name_length == FATX_DIRENT_END2)
            {
                return false;
            }
            if (seen++ == index)
            {
                if (offset != NULL)
                {
                    *offset = at;
                }
                return true;
            }
        }
    }
    return false;
}

bool FatxWriter::SetEntry(uint64_t offset, const FatxDirent &entry)
{
    return m_device.Write(offset, &entry, sizeof(entry));
}

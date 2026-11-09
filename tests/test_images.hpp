#ifndef TEST_IMAGES_H_
#define TEST_IMAGES_H_

// Helpers for tests that build FATX images: a sparse disk (so partitions can
// be hundreds of MB), a disk that simulates a power cut, and a fingerprint
// of every file and folder.

#include <gtest/gtest.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "fatx_check.hpp"
#include "fatx_resize.hpp"
#include "fatx_writer.hpp"

namespace
{

const uint64_t MB = 1024 * 1024;
const uint32_t CLUSTER = 16 * 1024;

// Unwritten sectors read as 0
class SparseDevice : public FatxDevice
{
  public:
    bool Read(uint64_t offset, void *buffer, uint32_t length) override
    {
        unsigned char *out = (unsigned char *)buffer;
        while (length > 0)
        {
            const uint64_t sector = offset / HDD_SECTOR_SIZE;
            const uint32_t at = (uint32_t)(offset % HDD_SECTOR_SIZE);
            const uint32_t part = (length < HDD_SECTOR_SIZE - at) ? length : HDD_SECTOR_SIZE - at;
            const auto found = m_sectors.find(sector);
            if (found == m_sectors.end())
            {
                memset(out, 0, part);
            }
            else
            {
                memcpy(out, found->second.data() + at, part);
            }
            out += part;
            offset += part;
            length -= part;
        }
        return true;
    }

    bool Write(uint64_t offset, const void *buffer, uint32_t length) override
    {
        const unsigned char *in = (const unsigned char *)buffer;
        while (length > 0)
        {
            const uint64_t sector = offset / HDD_SECTOR_SIZE;
            const uint32_t at = (uint32_t)(offset % HDD_SECTOR_SIZE);
            const uint32_t part = (length < HDD_SECTOR_SIZE - at) ? length : HDD_SECTOR_SIZE - at;
            std::vector<unsigned char> &data = m_sectors[sector];
            data.resize(HDD_SECTOR_SIZE, 0);
            memcpy(data.data() + at, in, part);
            in += part;
            offset += part;
            length -= part;
        }
        return true;
    }

  private:
    std::map<uint64_t, std::vector<unsigned char>> m_sectors;
};

// Simulates a power cut: writes after the limit are lost, and the one it
// happens in only gets its first half of sectors through
class PowerCutDevice : public FatxDevice
{
  public:
    PowerCutDevice(FatxDevice &inner, uint64_t limit) : m_inner(inner), m_limit(limit), m_writes(0)
    {
    }

    bool Read(uint64_t offset, void *buffer, uint32_t length) override
    {
        return m_inner.Read(offset, buffer, length);
    }

    bool Write(uint64_t offset, const void *buffer, uint32_t length) override
    {
        if (m_writes++ < m_limit)
        {
            return m_inner.Write(offset, buffer, length);
        }
        if (m_writes == m_limit + 1)
        {
            const uint32_t sectors = length / HDD_SECTOR_SIZE / 2;
            if (sectors > 0)
            {
                m_inner.Write(offset, buffer, sectors * HDD_SECTOR_SIZE);
            }
        }
        return false;
    }

    uint64_t Writes() const
    {
        return m_writes;
    }

  private:
    FatxDevice &m_inner;
    uint64_t m_limit;
    uint64_t m_writes;
};

// Stands in for the partition table
class SizeRecord : public FatxResizeTable
{
  public:
    explicit SizeRecord(uint64_t lba) : size(lba), calls(0), fail(false)
    {
    }
    bool SetSize(uint64_t lba) override
    {
        calls++;
        if (fail)
        {
            return false;
        }
        size = lba;
        return true;
    }
    uint64_t size;
    int calls;
    bool fail; // Like a table write error on the Xbox
};

// Attributes and times
std::string Details(const FatxDirent &entry)
{
    return std::to_string(entry.attributes & ~FATX_ATTR_DIRECTORY) + "/" + std::to_string(entry.modified_date) + "/" + std::to_string(entry.modified_time) +
           "/" + std::to_string(entry.created_date) + "/" + std::to_string(entry.created_time) + "/" + std::to_string(entry.accessed_date) + "/" +
           std::to_string(entry.accessed_time);
}

// Every file and folder, with a hash of its contents, by path
void Walk(FatxWriter &writer, FatxDevice &device, uint32_t dir, const std::string &path, std::map<std::string, std::string> &out)
{
    for (uint32_t i = 0;; i++)
    {
        FatxDirent entry;
        if (!writer.GetEntry(dir, i, &entry))
        {
            return;
        }
        if (entry.name_length == FATX_DIRENT_DELETED)
        {
            continue;
        }
        const std::string name = path + "\\" + std::string(entry.name, entry.name_length);
        if (entry.attributes & FATX_ATTR_DIRECTORY)
        {
            out[name] = "folder:" + Details(entry);
            Walk(writer, device, entry.first_cluster, name, out);
            continue;
        }

        uint32_t hash = 2166136261u;
        uint32_t left = entry.file_size;
        uint32_t cluster = entry.first_cluster;
        std::vector<unsigned char> data(writer.ClusterBytes());
        while (left > 0 && cluster >= 1 && cluster < writer.Layout().cluster_limit)
        {
            const uint32_t part = left < writer.ClusterBytes() ? left : writer.ClusterBytes();
            device.Read(writer.ClusterOffset(cluster), data.data(), part);
            for (uint32_t b = 0; b < part; b++)
            {
                hash = (hash ^ data[b]) * 16777619u;
            }
            left -= part;
            cluster = writer.GetFat(cluster);
        }
        out[name] = std::to_string(entry.file_size) + ":" + std::to_string(hash) + (left > 0 ? ":short" : "") + ":" + Details(entry);
    }
}

std::map<std::string, std::string> Contents(FatxDevice &device, uint64_t lba_size)
{
    std::map<std::string, std::string> out;
    FatxWriter writer(device, lba_size * HDD_SECTOR_SIZE);
    if (!writer.Load())
    {
        out["?"] = "not loadable";
        return out;
    }
    Walk(writer, device, writer.RootCluster(), "", out);
    return out;
}

FatxCheckResult Check(FatxDevice &device, uint64_t lba_size)
{
    FatxCheckResult result;
    FatxCheck(device, lba_size * HDD_SECTOR_SIZE, &result);
    return result;
}

std::vector<unsigned char> Pattern(uint32_t size, uint32_t seed)
{
    std::vector<unsigned char> data(size);
    for (uint32_t i = 0; i < size; i++)
    {
        data[i] = (unsigned char)((i * 31 + seed * 7 + (i >> 9)) & 0xFF);
    }
    return data;
}

void AddFile(FatxWriter &writer, uint32_t dir, const char *name, uint32_t size, uint32_t seed, uint32_t *first = NULL)
{
    const std::vector<unsigned char> data = Pattern(size, seed);
    ASSERT_TRUE(writer.AddFile(dir, name, data.data(), size, first)) << name;
}

void ExpectSameFiles(const std::map<std::string, std::string> &before, const std::map<std::string, std::string> &after)
{
    EXPECT_EQ(before.size(), after.size());
    for (const auto &file : before)
    {
        const auto found = after.find(file.first);
        if (found == after.end())
        {
            ADD_FAILURE() << file.first << " is gone";
        }
        else
        {
            EXPECT_EQ(file.second, found->second) << file.first;
        }
    }
}

} // namespace

#endif // TEST_IMAGES_H_

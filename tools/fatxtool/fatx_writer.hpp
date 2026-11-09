#ifndef FATX_WRITER_H_
#define FATX_WRITER_H_

// Creates and edits FATX images on the PC, for fatxtool and the tests. Keeps
// the whole FAT in memory.

#include "fatx_check.hpp"
#include <vector>

// In memory, for the tests
class FatxMemoryDevice : public FatxDevice
{
  public:
    explicit FatxMemoryDevice(uint64_t size) : m_data((size_t)size, 0)
    {
    }
    bool Read(uint64_t offset, void *buffer, uint32_t length) override;
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override;
    unsigned char *Data()
    {
        return m_data.data();
    }
    uint64_t Size() const
    {
        return m_data.size();
    }

  private:
    std::vector<unsigned char> m_data;
};

// Supplies a file's contents in order
class FatxSource
{
  public:
    virtual ~FatxSource()
    {
    }
    virtual bool Read(void *buffer, uint32_t length) = 0;
};

class FatxWriter
{
  public:
    // cluster_bytes 0 picks XBpartitioner's size
    FatxWriter(FatxDevice &device, uint64_t partition_bytes, uint32_t cluster_bytes = 0);

    // Format() creates a file system; Load() opens an existing one
    bool Format(uint32_t serial = 0x12345678);
    bool Load();
    // Writes the FAT; call when done
    bool Flush();

    uint32_t RootCluster() const
    {
        return m_root;
    }
    uint32_t ClusterBytes() const
    {
        return m_cluster_bytes;
    }
    const FatxLayout &Layout() const
    {
        return m_layout;
    }
    // Where allocation starts searching (tests use it to place files)
    void SetNextFree(uint32_t cluster)
    {
        m_next_free = cluster;
    }
    uint64_t ClusterOffset(uint32_t cluster) const;

    // parent is a folder's first cluster
    bool MakeDirectory(uint32_t parent, const char *name, uint32_t *cluster = NULL);
    bool AddFile(uint32_t parent, const char *name, uint32_t size, FatxSource &source, uint32_t *first_cluster = NULL);
    bool AddFile(uint32_t parent, const char *name, const void *data, uint32_t size, uint32_t *first_cluster = NULL);
    // Copy name, attributes, times and size from another entry
    bool MakeDirectory(uint32_t parent, const FatxDirent &like, uint32_t *cluster = NULL);
    bool AddFile(uint32_t parent, const FatxDirent &like, FatxSource &source, uint32_t *first_cluster = NULL);

    // Raw access, for breaking images in tests
    uint32_t GetFat(uint32_t cluster) const;
    void SetFat(uint32_t cluster, uint32_t value);
    // The index-th entry of a folder, counting deleted ones
    bool GetEntry(uint32_t dir, uint32_t index, FatxDirent *entry, uint64_t *offset = NULL);
    bool SetEntry(uint64_t offset, const FatxDirent &entry);

  private:
    bool AllocateChain(uint32_t count, uint32_t *first);
    bool FillCluster(uint32_t cluster, unsigned char value);
    bool AddEntry(uint32_t parent, const FatxDirent &entry);
    bool MakeEntry(const char *name, unsigned char attributes, uint32_t first, uint32_t size, FatxDirent *entry);
    // Writes data to a new chain (*first is 0 for empty files)
    bool WriteChain(uint32_t size, FatxSource &source, uint32_t *first);

    FatxDevice &m_device;
    uint64_t m_partition_bytes;
    uint32_t m_cluster_bytes;
    FatxLayout m_layout;
    uint32_t m_root;
    uint32_t m_next_free; // Allocation search start
    std::vector<uint32_t> m_fat;
};

#endif // FATX_WRITER_H_

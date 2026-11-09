#ifndef FATX_WRITER_H_
#define FATX_WRITER_H_

// Makes FATX partition images on the PC: formats them and adds directories
// and files. Used by fatxtool and the tests (which then break them on
// purpose). Not for the Xbox: it keeps the whole FAT in memory.

#include "fatx_check.hpp"
#include <vector>

// In memory, for the tests
class FatxMemoryDevice : public FatxDevice
{
public:
    explicit FatxMemoryDevice(uint64_t size) : m_data((size_t)size, 0) {}
    bool Read(uint64_t offset, void *buffer, uint32_t length) override;
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override;
    unsigned char *Data() { return m_data.data(); }
    uint64_t Size() const { return m_data.size(); }

private:
    std::vector<unsigned char> m_data;
};

// Where a file's contents come from, read in order
class FatxSource
{
public:
    virtual ~FatxSource() {}
    virtual bool Read(void *buffer, uint32_t length) = 0;
};

class FatxWriter
{
public:
    // cluster_bytes 0: what XBpartitioner picks for the size
    FatxWriter(FatxDevice &device, uint64_t partition_bytes, uint32_t cluster_bytes = 0);

    // A new empty file system; Load() for one that is already there
    bool Format(uint32_t serial = 0x12345678);
    bool Load();
    // Writes the FAT back; call it when done
    bool Flush();

    uint32_t RootCluster() const { return m_root; }
    uint32_t ClusterBytes() const { return m_cluster_bytes; }
    const FatxLayout &Layout() const { return m_layout; }
    uint64_t ClusterOffset(uint32_t cluster) const;

    // parent: a directory's first cluster. *cluster: the new one's.
    bool MakeDirectory(uint32_t parent, const char *name, uint32_t *cluster = NULL);
    bool AddFile(uint32_t parent, const char *name, uint32_t size, FatxSource &source, uint32_t *first_cluster = NULL);
    bool AddFile(uint32_t parent, const char *name, const void *data, uint32_t size, uint32_t *first_cluster = NULL);

    // Raw access, for breaking images on purpose. 32 bit values, see FATX_FAT_END.
    uint32_t GetFat(uint32_t cluster) const;
    void SetFat(uint32_t cluster, uint32_t value);
    // The index-th used entry of a directory (deleted ones count), false if none
    bool GetEntry(uint32_t dir, uint32_t index, FatxDirent *entry, uint64_t *offset = NULL);
    bool SetEntry(uint64_t offset, const FatxDirent &entry);

private:
    bool AllocateChain(uint32_t count, uint32_t *first);
    bool FillCluster(uint32_t cluster, unsigned char value);
    bool AddEntry(uint32_t parent, const FatxDirent &entry);
    bool MakeEntry(const char *name, unsigned char attributes, uint32_t first, uint32_t size, FatxDirent *entry);

    FatxDevice &m_device;
    uint64_t m_partition_bytes;
    uint32_t m_cluster_bytes;
    FatxLayout m_layout;
    uint32_t m_root;
    uint32_t m_next_free; // Where the search for a free cluster starts
    std::vector<uint32_t> m_fat;
};

#endif // FATX_WRITER_H_

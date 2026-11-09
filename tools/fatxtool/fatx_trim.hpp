#ifndef FATX_TRIM_H_
#define FATX_TRIM_H_

// TRIM for an Xbox drive connected to a PC. The Xbox kernel can't send it
// (its IDE pass-through has no 48 bit DMA commands).

#include "fatx_check.hpp"
#include "fatx_recluster.hpp" // FatxRange
#include <vector>

// Lists a partition's free clusters as byte ranges. Refuses if the check
// finds errors, since a "free" cluster could then still hold file data.
// Only clusters are listed: never the header, the FAT or the unused tail,
// where other tools keep a backup table.
bool FatxFreeRanges(FatxReader &reader, uint64_t partition_bytes, std::vector<FatxRange> *ranges, FatxCheckResult *check);

// Reads a partition inside a disk
class FatxOffsetReader : public FatxReader
{
  public:
    FatxOffsetReader(FatxReader &inner, uint64_t base) : m_inner(inner), m_base(base)
    {
    }
    bool Read(uint64_t offset, void *buffer, uint32_t length) override
    {
        return m_inner.Read(m_base + offset, buffer, length);
    }

  private:
    FatxReader &m_inner;
    uint64_t m_base;
};

// A physical disk on Windows (\\.\PhysicalDriveN; needs administrator
// rights), or a file (for the tests). Unbuffered: reads and writes go
// straight to the disk, in whole 4 KB blocks; a write that only covers part
// of a block reads it first. TRIMs through ATA pass-through.
class WindowsDisk : public FatxDevice
{
  public:
    WindowsDisk();
    ~WindowsDisk();

    // write: for Write() and for ATA commands (TRIM needs it, even IDENTIFY)
    bool Open(const char *path, bool write, unsigned long *error);
    uint64_t Size() const
    {
        return m_size;
    }
    bool Read(uint64_t offset, void *buffer, uint32_t length) override;
    // Only when opened for writing
    bool Write(uint64_t offset, const void *buffer, uint32_t length) override;
    // Flushes the drive's write cache
    bool Sync() override;

    // ATA IDENTIFY; fails if the connection blocks ATA commands (often USB)
    bool Identify(uint16_t identify[256]);
    // TRIMs sector-aligned byte ranges. max_blocks comes from IDENTIFY word 105.
    bool Trim(const std::vector<FatxRange> &ranges, uint32_t max_blocks, FatxProgress *progress);

  private:
    bool Command(void *task, unsigned char *data, uint32_t length, bool data_out, bool dma, bool lba48);
    // The whole blocks around length bytes at offset that fit the buffer
    void Blocks(uint64_t offset, uint32_t length, uint64_t *begin, uint32_t *skip, uint32_t *part, uint32_t *whole) const;
    bool Transfer(uint64_t begin, uint32_t whole, bool write);

    void *m_handle;
    bool m_writable;
    uint64_t m_size;
    unsigned char *m_buffer; // Page aligned, for unbuffered I/O
};

#endif // FATX_TRIM_H_

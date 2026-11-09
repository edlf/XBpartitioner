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
bool FatxFreeRanges(FatxReader &reader, uint64_t partition_bytes, std::vector<FatxRange> *ranges,
                    FatxCheckResult *check);

// Reads a partition inside a disk
class FatxOffsetReader : public FatxReader
{
public:
    FatxOffsetReader(FatxReader &inner, uint64_t base) : m_inner(inner), m_base(base) {}
    bool Read(uint64_t offset, void *buffer, uint32_t length) override
    {
        return m_inner.Read(m_base + offset, buffer, length);
    }

private:
    FatxReader &m_inner;
    uint64_t m_base;
};

// A physical disk on Windows (\\.\PhysicalDriveN; needs administrator
// rights). Reads, and TRIMs through ATA pass-through. Never writes data.
class WindowsDisk : public FatxReader
{
public:
    WindowsDisk();
    ~WindowsDisk();

    // trim opens it for ATA commands too
    bool Open(const char *path, bool trim, unsigned long *error);
    uint64_t Size() const { return m_size; }
    bool Read(uint64_t offset, void *buffer, uint32_t length) override;

    // ATA IDENTIFY; fails if the connection blocks ATA commands (often USB)
    bool Identify(uint16_t identify[256]);
    // TRIMs sector-aligned byte ranges. max_blocks comes from IDENTIFY word 105.
    bool Trim(const std::vector<FatxRange> &ranges, uint32_t max_blocks, FatxProgress *progress);

private:
    bool Command(void *task, unsigned char *data, uint32_t length, bool data_out, bool dma, bool lba48);

    void *m_handle;
    uint64_t m_size;
    unsigned char *m_buffer; // Page aligned, for unbuffered I/O
};

#endif // FATX_TRIM_H_

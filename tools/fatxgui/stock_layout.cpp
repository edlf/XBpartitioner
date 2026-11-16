// See stock_layout.hpp

#define NOMINMAX
#include "stock_layout.hpp"

#include <string.h>

#include "fatx_trim.hpp" // FatxOffsetReader

namespace fatxgui
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

bool has_fatx_at_start(FatxReader &reader)
{
    const uint32_t signature = read_signature(reader);
    return signature == FATX_signature || signature == FATX_RESIZING_SIGNATURE;
}

bool read_stock_parts(FatxReader &disk, std::vector<Part> *parts)
{
    parts->clear();
    const PartitionTableEditor stock(0, false, false);
    bool found_fatx = false;
    for (int i = 0; i < XBOX_STANDARD_PARTITION_COUNT; i++)
    {
        Part part;
        part.index = i;
        part.start = stock.GetPartitionStart(i);
        part.sectors = stock.GetPartitionSize(i);
        part.max_sectors = part.sectors; // No table to hold a new size
        FatxOffsetReader reader(disk, part.start * HDD_SECTOR_SIZE);
        found_fatx = has_fatx_at_start(reader) || found_fatx;
        parts->push_back(part);
    }
    return found_fatx;
}

} // namespace fatxgui

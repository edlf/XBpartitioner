#include "fatx_format.hpp"
#include <stdlib.h>
#include <string.h>

// FAT bytes written per call; a multiple of the 4 KB the FAT is rounded to
static const uint32_t FORMAT_CHUNK_BYTES = 64 * 1024;

static const uint32_t FORMAT_ROOT_CLUSTER = 1;

bool FatxFormat(FatxDevice &device, uint64_t lba_size, uint32_t cluster_bytes, uint32_t serial, FatxProgress *progress)
{
    // Down to a sector, so tests can make 32 bit FATs in small images
    if (cluster_bytes < HDD_SECTOR_SIZE || cluster_bytes > 1024 * 1024 || (cluster_bytes & (cluster_bytes - 1)) != 0)
    {
        return false;
    }
    const FatxLayout layout = FatxLayoutFor(lba_size, cluster_bytes);
    if (layout.cluster_limit < 2)
    {
        return false; // No room for the root folder
    }

    // The chunk buffer, also used for the header and the root folder
    const uint32_t buffer_bytes = (cluster_bytes > FORMAT_CHUNK_BYTES) ? cluster_bytes : FORMAT_CHUNK_BYTES;
    unsigned char *buffer = (unsigned char *)malloc(buffer_bytes);
    if (buffer == NULL)
    {
        return false;
    }
    bool ok = true;

    // 1. Wipe the old header, so the old file system is gone from here on
    memset(buffer, 0xFF, FATX_HEADER_BYTES);
    ok = device.Write(0, buffer, FATX_HEADER_BYTES) && device.Sync();

    // 2. The FAT: entry 0 is the media marker, entry 1 (the root) its chain's
    // end, the rest free
    uint64_t written = 0;
    while (ok && written < layout.bytes_per_fat)
    {
        const uint64_t left = layout.bytes_per_fat - written;
        const uint32_t length = (left < FORMAT_CHUNK_BYTES) ? (uint32_t)left : FORMAT_CHUNK_BYTES;
        memset(buffer, 0, length);
        if (written == 0)
        {
            const uint32_t width = layout.bytes_per_entry;
            const uint32_t media = (width == 2) ? (FATX_FAT_MEDIA & 0xFFFF) : FATX_FAT_MEDIA;
            const uint32_t end = (width == 2) ? (FATX_FAT_END & 0xFFFF) : FATX_FAT_END;
            memcpy(buffer, &media, width);
            memcpy(buffer + width * FORMAT_ROOT_CLUSTER, &end, width);
        }
        ok = device.Write(FATX_HEADER_BYTES + written, buffer, length);
        written += length;
        if (progress != NULL)
        {
            progress->Update((int)(written * 95 / layout.bytes_per_fat));
        }
    }

    // 3. The root folder: no entries (0xFF ends the list)
    if (ok)
    {
        memset(buffer, FATX_DIRENT_END, cluster_bytes);
        ok = device.Write(layout.data_offset + (uint64_t)(FORMAT_ROOT_CLUSTER - 1) * cluster_bytes, buffer, cluster_bytes);
    }

    // 4. The header last, once everything it points to is in place.
    // 0x00 "FATX", 0x04 serial, 0x08 sectors per cluster, 0x0C root cluster,
    // 0x10 a 16 bit 0; the rest of the 4 KB stays 0xFF.
    if (ok)
    {
        memset(buffer, 0xFF, FATX_HEADER_BYTES);
        const uint32_t signature = FATX_signature;
        const uint32_t sectors_per_cluster = cluster_bytes / HDD_SECTOR_SIZE;
        const uint32_t root = FORMAT_ROOT_CLUSTER;
        const uint16_t zero = 0;
        memcpy(buffer + FATX_SIGNATURE_OFFSET, &signature, 4);
        memcpy(buffer + 4, &serial, 4);
        memcpy(buffer + FATX_SECTORS_PER_CLUSTER_OFFSET, &sectors_per_cluster, 4);
        memcpy(buffer + FATX_ROOT_CLUSTER_OFFSET, &root, 4);
        memcpy(buffer + 16, &zero, 2);
        ok = device.Sync() && device.Write(0, buffer, FATX_HEADER_BYTES) && device.Sync();
    }

    free(buffer);
    if (ok && progress != NULL)
    {
        progress->Update(100);
    }
    return ok;
}

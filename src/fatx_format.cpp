#include "fatx_format.hpp"
#include <stdlib.h>
#include <string.h>

// FAT bytes written per call; a multiple of the 4 KB the FAT is rounded to
static const uint32_t FORMAT_CHUNK_BYTES = 64 * 1024;

static const uint32_t FORMAT_ROOT_CLUSTER = 1;

// A format under way. The buffer holds a FAT chunk, the header or the root
// folder, whichever is bigger.
struct FormatJob
{
    FatxDevice &device;
    FatxLayout layout;
    uint32_t cluster_bytes;
    uint32_t sectors_per_cluster; // As the header gives it, see FatxClusterBytes()
    unsigned char *buffer;
    FatxProgress *progress; // Or NULL
};

// 1. The old file system is gone from here on
static bool wipe_header(const FormatJob &job)
{
    memset(job.buffer, 0xFF, FATX_HEADER_BYTES);
    return job.device.Write(0, job.buffer, FATX_HEADER_BYTES) && job.device.Sync();
}

// Entry 0 is the media marker, entry 1 (the root) its chain's end
static void put_first_fat_entries(unsigned char *chunk, uint32_t width)
{
    const uint32_t media = (width == 2) ? (FATX_FAT_MEDIA & 0xFFFF) : FATX_FAT_MEDIA;
    const uint32_t end = (width == 2) ? (FATX_FAT_END & 0xFFFF) : FATX_FAT_END;
    memcpy(chunk, &media, width);
    memcpy(chunk + width * FORMAT_ROOT_CLUSTER, &end, width);
}

// 2. The FAT, every other entry free; up to 95% of the progress
static bool write_fat(const FormatJob &job)
{
    const uint64_t fat_bytes = job.layout.bytes_per_fat;
    for (uint64_t written = 0; written < fat_bytes;)
    {
        const uint64_t left = fat_bytes - written;
        const uint32_t length = (left < FORMAT_CHUNK_BYTES) ? (uint32_t)left : FORMAT_CHUNK_BYTES;
        memset(job.buffer, 0, length);
        if (written == 0)
        {
            put_first_fat_entries(job.buffer, job.layout.bytes_per_entry);
        }
        if (!job.device.Write(FATX_HEADER_BYTES + written, job.buffer, length))
        {
            return false;
        }
        written += length;
        if (job.progress != NULL)
        {
            job.progress->Update((int)(written * 95 / fat_bytes));
        }
    }
    return true;
}

// 3. No entries (0xFF ends the list)
static bool write_empty_root(const FormatJob &job)
{
    memset(job.buffer, FATX_DIRENT_END, job.cluster_bytes);
    return job.device.Write(job.layout.data_offset + (uint64_t)(FORMAT_ROOT_CLUSTER - 1) * job.cluster_bytes, job.buffer, job.cluster_bytes);
}

// 4. Last, once everything it points to is in place. 0x00 "FATX", 0x04
// serial, 0x08 sectors per cluster, 0x0C root cluster, 0x10 a 16 bit 0; the
// rest of the 4 KB stays 0xFF.
static bool write_header(const FormatJob &job, uint32_t serial)
{
    memset(job.buffer, 0xFF, FATX_HEADER_BYTES);
    const uint32_t signature = FATX_signature;
    const uint32_t sectors_per_cluster = job.sectors_per_cluster;
    const uint32_t root = FORMAT_ROOT_CLUSTER;
    const uint16_t zero = 0;
    memcpy(job.buffer + FATX_SIGNATURE_OFFSET, &signature, 4);
    memcpy(job.buffer + 4, &serial, 4);
    memcpy(job.buffer + FATX_SECTORS_PER_CLUSTER_OFFSET, &sectors_per_cluster, 4);
    memcpy(job.buffer + FATX_ROOT_CLUSTER_OFFSET, &root, 4);
    memcpy(job.buffer + 16, &zero, 2);
    return job.device.Sync() && job.device.Write(0, job.buffer, FATX_HEADER_BYTES) && job.device.Sync();
}

static bool format_volume(FatxDevice &device, uint64_t lba_size, uint32_t cluster_bytes, uint32_t serial, bool memory_unit, FatxProgress *progress)
{
    const uint32_t sectors_per_cluster = FatxSectorsPerCluster(cluster_bytes, memory_unit);
    if (sectors_per_cluster == 0)
    {
        return false;
    }
    const FatxLayout layout = FatxLayoutFor(lba_size, cluster_bytes);
    if (layout.cluster_limit < 2)
    {
        return false; // No room for the root folder
    }

    const uint32_t buffer_bytes = (cluster_bytes > FORMAT_CHUNK_BYTES) ? cluster_bytes : FORMAT_CHUNK_BYTES;
    unsigned char *buffer = (unsigned char *)malloc(buffer_bytes);
    if (buffer == NULL)
    {
        return false;
    }
    const FormatJob job = {device, layout, cluster_bytes, sectors_per_cluster, buffer, progress};
    // Each step only once the one before it worked
    const bool ok = wipe_header(job) && write_fat(job) && write_empty_root(job) && write_header(job, serial);
    free(buffer);

    if (ok && progress != NULL)
    {
        progress->Update(100);
    }
    return ok;
}

bool FatxFormat(FatxDevice &device, uint64_t lba_size, uint32_t cluster_bytes, uint32_t serial, FatxProgress *progress)
{
    return format_volume(device, lba_size, cluster_bytes, serial, false, progress);
}

bool FatxFormatMemoryUnit(FatxDevice &device, uint64_t lba_size, uint32_t cluster_bytes, uint32_t serial, FatxProgress *progress)
{
    return format_volume(device, lba_size, cluster_bytes, serial, true, progress);
}

// Lean, because the full windows.h pulls in <winioctl.h>, which would pick
// up the Xbox version in src\. The definitions needed are below.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "fatx_trim.hpp"
#include <string.h>

namespace
{

const DWORD IOCTL_DISK_GET_LENGTH = 0x0007405C;       // IOCTL_DISK_GET_LENGTH_INFO
const DWORD IOCTL_ATA_PASS_THROUGH_DIRECT_ = 0x0004D030; // IOCTL_ATA_PASS_THROUGH_DIRECT

const USHORT ATA_FLAG_DRDY_REQUIRED = 0x01;
const USHORT ATA_FLAG_DATA_IN = 0x02;
const USHORT ATA_FLAG_DATA_OUT = 0x04;
const USHORT ATA_FLAG_48BIT = 0x08;
const USHORT ATA_FLAG_USE_DMA = 0x10;

// ATA_PASS_THROUGH_DIRECT from ntddscsi.h
typedef struct
{
    USHORT Length;
    USHORT AtaFlags;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR ReservedAsUchar;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG ReservedAsUlong;
    PVOID DataBuffer;
    UCHAR PreviousTaskFile[8]; // High bytes of a 48 bit command
    UCHAR CurrentTaskFile[8];  // Features, count, LBA, device, command; status on return
} AtaPassThroughDirect;

const UCHAR ATA_IDENTIFY_DEVICE = 0xEC;
const UCHAR ATA_DATA_SET_MANAGEMENT = 0x06;
const UCHAR ATA_DSM_TRIM = 0x01;
const UCHAR ATA_STATUS_ERROR = 0x01;

const uint32_t BUFFER_BYTES = 1024 * 1024;
const uint64_t TRIM_ENTRY_MAX_SECTORS = 0xFFFF;
const uint32_t TRIM_ENTRIES_PER_BLOCK = HDD_SECTOR_SIZE / 8;

} // namespace

bool FatxFreeRanges(FatxReader &reader, uint64_t partition_bytes, std::vector<FatxRange> *ranges, FatxCheckResult *check)
{
    ranges->clear();
    FatxCheck(reader, partition_bytes, check);
    if (check->status != FATX_CHECK_DONE || check->errors > 0)
    {
        return false;
    }

    const FatxLayout &layout = check->layout;
    const uint32_t cluster_bytes = check->cluster_bytes;
    std::vector<unsigned char> chunk(0x10000);
    uint64_t chunk_offset = ~0ULL;
    uint64_t run_first = 0; // Start of the current free run, or 0
    for (uint64_t cluster = 1; cluster <= layout.cluster_limit; cluster++)
    {
        bool free_cluster = false;
        if (cluster < layout.cluster_limit)
        {
            const uint64_t byte = cluster * layout.bytes_per_entry;
            const uint64_t offset = byte - byte % chunk.size();
            if (offset != chunk_offset)
            {
                const uint64_t left = layout.bytes_per_fat - offset;
                const uint32_t length = (uint32_t)(left < chunk.size() ? left : chunk.size());
                if (!reader.Read(FATX_HEADER_BYTES + offset, chunk.data(), length))
                {
                    return false;
                }
                chunk_offset = offset;
            }
            uint32_t value = 0;
            memcpy(&value, chunk.data() + (byte - offset), layout.bytes_per_entry);
            free_cluster = (value == FATX_FAT_FREE);
        }

        if (free_cluster && run_first == 0)
        {
            run_first = cluster;
        }
        else if (!free_cluster && run_first != 0)
        {
            FatxRange range;
            range.offset = layout.data_offset + (run_first - 1) * cluster_bytes;
            range.length = (cluster - run_first) * cluster_bytes;
            ranges->push_back(range);
            run_first = 0;
        }
    }
    return true;
}

WindowsDisk::WindowsDisk() : m_handle(INVALID_HANDLE_VALUE), m_size(0), m_buffer(NULL)
{
}

WindowsDisk::~WindowsDisk()
{
    if (m_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle((HANDLE)m_handle);
    }
    if (m_buffer != NULL)
    {
        VirtualFree(m_buffer, 0, MEM_RELEASE);
    }
}

bool WindowsDisk::Open(const char *path, bool trim, unsigned long *error)
{
    // ATA pass-through needs write access, even for IDENTIFY
    HANDLE handle = CreateFileA(path, GENERIC_READ | (trim ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        *error = GetLastError();
        return false;
    }
    m_handle = handle;

    LARGE_INTEGER length;
    DWORD returned = 0;
    m_buffer = (unsigned char *)VirtualAlloc(NULL, BUFFER_BYTES, MEM_COMMIT, PAGE_READWRITE);
    if (m_buffer == NULL ||
        !DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH, NULL, 0, &length, sizeof(length), &returned, NULL))
    {
        *error = GetLastError();
        return false;
    }
    m_size = (uint64_t)length.QuadPart;
    return true;
}

bool WindowsDisk::Read(uint64_t offset, void *buffer, uint32_t length)
{
    // Unbuffered reads must be whole, aligned sectors
    unsigned char *out = (unsigned char *)buffer;
    while (length > 0)
    {
        const uint64_t begin = offset - offset % HDD_SECTOR_SIZE;
        const uint32_t skip = (uint32_t)(offset - begin);
        uint32_t part = BUFFER_BYTES - skip;
        part = (part < length) ? part : length;
        const uint32_t whole = (skip + part + HDD_SECTOR_SIZE - 1) / HDD_SECTOR_SIZE * HDD_SECTOR_SIZE;

        LARGE_INTEGER position;
        position.QuadPart = (LONGLONG)begin;
        DWORD done = 0;
        if (!SetFilePointerEx((HANDLE)m_handle, position, NULL, FILE_BEGIN) ||
            !ReadFile((HANDLE)m_handle, m_buffer, whole, &done, NULL) || done != whole)
        {
            return false;
        }
        memcpy(out, m_buffer + skip, part);
        out += part;
        offset += part;
        length -= part;
    }
    return true;
}

bool WindowsDisk::Command(void *task, unsigned char *data, uint32_t length, bool data_out, bool dma, bool lba48)
{
    AtaPassThroughDirect command;
    memset(&command, 0, sizeof(command));
    command.Length = sizeof(command);
    command.AtaFlags = ATA_FLAG_DRDY_REQUIRED | (data_out ? ATA_FLAG_DATA_OUT : ATA_FLAG_DATA_IN) |
                       (dma ? ATA_FLAG_USE_DMA : 0) | (lba48 ? ATA_FLAG_48BIT : 0);
    command.DataTransferLength = length;
    command.TimeOutValue = 60;
    command.DataBuffer = data;
    memcpy(command.PreviousTaskFile, task, 8);
    memcpy(command.CurrentTaskFile, (unsigned char *)task + 8, 8);

    DWORD returned = 0;
    if (!DeviceIoControl((HANDLE)m_handle, IOCTL_ATA_PASS_THROUGH_DIRECT_, &command, sizeof(command), &command,
                         sizeof(command), &returned, NULL))
    {
        return false;
    }
    return (command.CurrentTaskFile[6] & ATA_STATUS_ERROR) == 0;
}

bool WindowsDisk::Identify(uint16_t identify[256])
{
    unsigned char task[16];
    memset(task, 0, sizeof(task));
    task[8 + 6] = ATA_IDENTIFY_DEVICE;
    memset(m_buffer, 0, HDD_SECTOR_SIZE);
    if (!Command(task, m_buffer, HDD_SECTOR_SIZE, false, false, false))
    {
        return false;
    }
    memcpy(identify, m_buffer, HDD_SECTOR_SIZE);
    return true;
}

bool WindowsDisk::Trim(const std::vector<FatxRange> &ranges, uint32_t max_blocks, FatxProgress *progress)
{
    // Each entry: 48 bit LBA, 16 bit sector count
    std::vector<uint64_t> entries;
    for (const FatxRange &range : ranges)
    {
        uint64_t lba = range.offset / HDD_SECTOR_SIZE;
        uint64_t sectors = range.length / HDD_SECTOR_SIZE;
        while (sectors > 0)
        {
            const uint64_t count = (sectors < TRIM_ENTRY_MAX_SECTORS) ? sectors : TRIM_ENTRY_MAX_SECTORS;
            entries.push_back((lba & 0xFFFFFFFFFFFFULL) | (count << 48));
            lba += count;
            sectors -= count;
        }
    }

    // Stay small; some drives overstate what they accept
    max_blocks = (max_blocks < 1) ? 1 : (max_blocks > 8 ? 8 : max_blocks);
    const size_t per_command = (size_t)max_blocks * TRIM_ENTRIES_PER_BLOCK;
    for (size_t first = 0; first < entries.size(); first += per_command)
    {
        const size_t count = (entries.size() - first < per_command) ? entries.size() - first : per_command;
        const uint32_t blocks = (uint32_t)((count + TRIM_ENTRIES_PER_BLOCK - 1) / TRIM_ENTRIES_PER_BLOCK);
        memset(m_buffer, 0, blocks * HDD_SECTOR_SIZE); // Unused entries are ignored
        memcpy(m_buffer, &entries[first], count * sizeof(uint64_t));

        unsigned char task[16];
        memset(task, 0, sizeof(task));
        task[1] = (unsigned char)(blocks >> 8);   // Count, high byte
        task[8 + 0] = ATA_DSM_TRIM;               // Features
        task[8 + 1] = (unsigned char)blocks;      // Count, in 512 byte blocks
        task[8 + 5] = 0x40;                       // LBA mode
        task[8 + 6] = ATA_DATA_SET_MANAGEMENT;
        if (!Command(task, m_buffer, blocks * HDD_SECTOR_SIZE, true, true, true))
        {
            return false;
        }
        if (progress != NULL)
        {
            progress->Update((int)((first + count) * 100 / entries.size()));
        }
    }
    return true;
}

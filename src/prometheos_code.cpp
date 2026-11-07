#include <Xtl.h>
#include <winbase.h>
#include "Winioctl.h"
#include "Undocumented.h"
#include "partition.hpp"

// Code stolen from PrometheOS

// Lowest set bit, e.g. 9 for 512. Plain C: the original "bsf eax" __asm
// returned through EAX with no return statement.
static ULONG FindFirstSetRightMember(ULONG Set)
{
    ULONG index = 0;
    while (Set != 0 && (Set & 1) == 0)
    {
        Set >>= 1;
        index++;
    }
    return index;
}

bool largePartitionFixup(PANSI_STRING partitionString, const ULONG clusterSize)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    DISK_GEOMETRY DiskGeometry;
    ULONG SectorShift;
    LARGE_INTEGER ByteOffset;
    LARGE_INTEGER Offset;
    ULONG ReadLength = ROUND_TO_PAGES(sizeof(FAT_VOLUME_METADATA));
    HANDLE VolumeHandle = NULL;
    PFAT_VOLUME_METADATA VolumeMetadata = NULL;
    bool ret = false;

    Offset.QuadPart = 0;

    InitializeObjectAttributes(&ObjectAttributes, partitionString, OBJ_CASE_INSENSITIVE, NULL);

    if (status = NtOpenFile(&VolumeHandle, SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE, &ObjectAttributes, &IoStatusBlock, (FILE_SHARE_READ | FILE_SHARE_WRITE),
                            FILE_SYNCHRONOUS_IO_ALERT | FILE_NO_INTERMEDIATE_BUFFERING))
    {
        goto CleanupAndReturn;
    }

    if (status = NtDeviceIoControlFile(VolumeHandle, NULL, NULL, NULL, &IoStatusBlock,
                                       IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &DiskGeometry,
                                       sizeof(DiskGeometry)))
    {
        goto CleanupAndReturn;
    }

    SectorShift = FindFirstSetRightMember(DiskGeometry.BytesPerSector);

    // partitionString is the partition itself, so the metadata is at offset 0
    // (PrometheOS opened the whole disk)
    Offset.QuadPart = 0;

    // Non-buffered I/O: use a page aligned buffer
    VolumeMetadata = (PFAT_VOLUME_METADATA)VirtualAlloc(NULL, ReadLength, MEM_COMMIT, PAGE_READWRITE);
    if (VolumeMetadata == NULL)
    {
        goto CleanupAndReturn;
    }

    ByteOffset = Offset;
    if (status = NtReadFile(VolumeHandle, NULL, NULL, NULL, &IoStatusBlock, VolumeMetadata, ReadLength, &ByteOffset))
    {
        goto CleanupAndReturn;
    }

    if (memcmp(&VolumeMetadata->Signature, "FATX", 4) == 0 && VolumeMetadata->SectorsPerCluster == 0)
    {
        VolumeMetadata->SectorsPerCluster = (ULONG)(clusterSize >> SectorShift);

        ByteOffset = Offset;
        if (status = NtWriteFile(VolumeHandle, NULL, NULL, NULL, &IoStatusBlock, VolumeMetadata, ReadLength, &ByteOffset))
        {
            goto CleanupAndReturn;
        }
    }

    ret = true;

CleanupAndReturn:
    if (VolumeHandle != NULL)
    {
        NtClose(VolumeHandle);
    }
    if (VolumeMetadata != NULL)
    {
        VirtualFree(VolumeMetadata, 0, MEM_RELEASE);
    }
    return ret;
}

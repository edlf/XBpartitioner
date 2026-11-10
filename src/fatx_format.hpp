#ifndef FATX_FORMAT_H_
#define FATX_FORMAT_H_

// Formats a partition as FATX the way the Xbox kernel lays it out (see
// FatxLayoutFor): a 4 KB header, an empty FAT, and an empty root folder in
// cluster 1. The FAT is written in small chunks, so it needs little memory
// whatever the partition's size.
//
// The old header is wiped first and the new one written last, so a format
// that stops partway leaves no FATX signature: the partition reads as
// unformatted, never as a mix of the old and new file systems.

#include "fatx_check.hpp"

// cluster_bytes: a power of two up to 1 MB; the Xbox uses 16 KB and up
// (ClusterSizeKbFor() gives XBpartitioner's choice), smaller ones are for
// tests. serial: the volume serial number in the header.
// The partition must not be mounted.
bool FatxFormat(FatxDevice &device, uint64_t lba_size, uint32_t cluster_bytes, uint32_t serial, FatxProgress *progress = NULL);

#endif // FATX_FORMAT_H_

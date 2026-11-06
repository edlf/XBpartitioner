#ifndef __PROMETHEOS_CODE_H__
#define __PROMETHEOS_CODE_H__
#include <Xtl.h>
#include <winbase.h>
#include "Undocumented.h"
#include "partition.hpp"

// Sets SectorsPerCluster in a freshly formatted FATX volume's metadata when the
// formatter left it at 0 (clusters of 128KB and up). partitionString is the
// partition device; the metadata is at offset 0 of it.
bool largePartitionFixup(PANSI_STRING partitionString, const ULONG clusterSize);

#endif

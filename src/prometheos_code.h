#ifndef __PROMETHEOS_CODE_H__
#define __PROMETHEOS_CODE_H__
#include <Xtl.h>
#include <winbase.h>
#include "Undocumented.h"
#include "partition.hpp"

// Sets SectorsPerCluster when the formatter left it at 0 (128 KB clusters
// and up). The metadata is at offset 0 of the partition device.
bool largePartitionFixup(PANSI_STRING partitionString, const ULONG clusterSize);

#endif

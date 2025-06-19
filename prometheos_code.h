#ifndef __PROMETHEOS_CODE_H__
#define __PROMETHEOS_CODE_H__
#include <Xtl.h>
#include <winbase.h>
#include "Undocumented.h"
#include "partition.hpp"

bool largePartitionFixup(PANSI_STRING partitionString, const XboxPartitionTableEntry entry, const ULONG clusterSize);

#endif

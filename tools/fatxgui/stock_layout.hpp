#ifndef FATXGUI_STOCK_LAYOUT_HPP_
#define FATXGUI_STOCK_LAYOUT_HPP_

// Finding FATX volumes on an image or drive without a partition table: one
// volume from sector 0, or the stock partitions at their fixed places

#include <vector>

#include "workspace.hpp"

namespace fatxgui
{

// The first 4 bytes of sector 0, or 0 if it can't be read
uint32_t read_signature(FatxReader &reader);

// A FATX volume (or one mid-resize) from sector 0
bool has_fatx_at_start(FatxReader &reader);

// The stock partitions (E, C, X, Y, Z) where the stock kernel finds them on a
// drive that never had a table; false unless one of them holds FATX
bool read_stock_parts(FatxReader &disk, std::vector<Part> *parts);

} // namespace fatxgui

#endif // FATXGUI_STOCK_LAYOUT_HPP_

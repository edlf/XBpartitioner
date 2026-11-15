#ifndef FATXGUI_TEXT_HPP_
#define FATXGUI_TEXT_HPP_

// Sizes and names as the UI writes them

#include <stdint.h>
#include <string>

namespace fatxgui
{

std::string utf8(const std::wstring &text);
std::string format(const char *fmt, ...);
// "1823.50 MB", like fatxtool
std::string megabytes_text(uint64_t lba);
// "931.51 GB"
std::string size_text(uint64_t bytes);
// "Partition 6 (F)", or "Partition" for a partition image
std::string partition_name(int index);

} // namespace fatxgui

#endif // FATXGUI_TEXT_HPP_

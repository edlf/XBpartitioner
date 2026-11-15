#define NOMINMAX
#include "text.hpp"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#include "pc_image.hpp" // partition_letter

namespace fatxgui
{

std::string utf8(const std::wstring &text)
{
    if (text.empty())
    {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], length, nullptr, nullptr);
    return out;
}

std::string format(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

// "1823.50 MB", like fatxtool
std::string megabytes_text(uint64_t lba)
{
    const uint64_t bytes = lba * HDD_SECTOR_SIZE;
    return format("%llu.%02llu MB", bytes >> 20, ((bytes % (1 << 20)) * 100) >> 20);
}

// "931.51 GB"
std::string size_text(uint64_t bytes)
{
    static const char *units[] = {"bytes", "KB", "MB", "GB", "TB"};
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        unit++;
    }
    return unit == 0 ? format("%llu bytes", bytes) : format("%.2f %s", value, units[unit]);
}

std::string partition_name(int index)
{
    return index < 0 ? std::string("Partition") : format("Partition %d (%c)", index + 1, partition_letter(index));
}

} // namespace fatxgui

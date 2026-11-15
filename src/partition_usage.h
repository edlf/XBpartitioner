#ifndef PARTITION_USAGE_H_
#define PARTITION_USAGE_H_

#include <string.h>
#include "fatx_resize.hpp"

// How much of a formatted partition is used, once its FAT has been scanned.
// Forgotten whenever the partition might have changed.
class PartitionUsage
{
  public:
    PartitionUsage() : m_known(false)
    {
        memset(&m_usage, 0, sizeof(m_usage));
    }

    bool IsKnown() const
    {
        return m_known;
    }

    // Only meaningful while IsKnown()
    const FatxUsage &Get() const
    {
        return m_usage;
    }

    void Remember(const FatxUsage &usage)
    {
        m_usage = usage;
        m_known = true;
    }

    void Forget()
    {
        m_known = false;
    }

  private:
    bool m_known;
    FatxUsage m_usage;
};

#endif // PARTITION_USAGE_H_

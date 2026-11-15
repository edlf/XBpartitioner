#ifndef ARRAY_LENGTH_H_
#define ARRAY_LENGTH_H_

#include <stddef.h>

// Element count of an array; does not compile for a pointer
template <typename T, size_t N> constexpr size_t ArrayLength(const T (&)[N])
{
    return N;
}

#endif // ARRAY_LENGTH_H_

#ifndef __KERNEL_BIN_UTILS_H_
#define __KERNEL_BIN_UTILS_H_
#include <stdint.h>

inline uint64_t strlen(const char *str)
{
    uint64_t len = 0;
    while (*str++)
    {
        len++;
    }
    return len;
}

inline bool compare(const char *a, const char *b)
{
    if (strlen(a) != strlen(b)) return false;
    while (*a && *b)
    {
        if (*a++ != *b++) return false;
    }
    return true;
}

#endif
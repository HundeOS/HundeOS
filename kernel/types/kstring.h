#ifndef __KERNEL_TYPES_KSTRING_H
#define __KERNEL_TYPES_KSTRING_H

#include <stdint.h>
#define MAX_SIZE 1024


typedef struct KString {
    uint64_t len;
    uint64_t max;
    char data[MAX_SIZE];
} KString;

KString KString__new(const char * str);
KString kstring(const char * str);

#endif
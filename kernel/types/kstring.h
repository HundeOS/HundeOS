#ifndef __KERNEL_TYPES_KSTRING_H
#define __KERNEL_TYPES_KSTRING_H

#include <stdint.h>
#include <stdbool.h>
#define MAX_SIZE 1024

#include "optional.h"


typedef struct KString {
    uint64_t len;
    uint64_t max;
    char data[MAX_SIZE];
} KString;

OptionalDef(KString);

typedef KString * KStringPtr;
OptionalDef(KStringPtr);

KString KString__new(const char * str);
KString kstring(const char * str);

bool KString__equals(KStringPtr self, KStringPtr other);
KString KString__sprintf(KStringPtr format, ...);
KString KString__append(KStringPtr self, KStringPtr other);
const char * KString__cstr(KString * str);

#define EMPTY_KSTRING \
    (KString) { \
        .len = 0, \
        .max = MAX_SIZE, \
        .data[0] = 0 \
    } \

#endif
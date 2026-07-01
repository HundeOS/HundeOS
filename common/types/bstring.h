#ifndef __COMMON_TYPES_BSTRING_H
#define __COMMON_TYPES_BSTRING_H

#include <stdbool.h>
#include <stdint.h>

#define BSTRING_MAX_SIZE 1024

typedef struct BString
{
    uint64_t len;
    uint64_t max;
    char data[BSTRING_MAX_SIZE];
}
BString;

typedef BString * BStringPtr;
typedef const BString * ConstBStringPtr;

BString BString__new(const char * str);
BString bstring(const char * str);
bool BString__equals(ConstBStringPtr self, ConstBStringPtr other);
BString BString__append(ConstBStringPtr self, ConstBStringPtr other);
bool BString__starts_with(ConstBStringPtr self, ConstBStringPtr prefix);
const char * BString__cstr(ConstBStringPtr self);

#define EMPTY_BSTRING \
    (BString) { \
        .len = 0, \
        .max = BSTRING_MAX_SIZE, \
        .data[0] = 0 \
    } \

#define BString__STRLEN(s) ( (sizeof(s)/sizeof(s[0])) - sizeof(s[0]) )
#define BSTRING_NEW(chars) \
    (BString) { \
        .max = BSTRING_MAX_SIZE, \
        .len = BString__STRLEN(chars), \
        .data = chars, \
    } \

#endif

#ifndef __KERNEL_TYPES_KSTRING_H
#define __KERNEL_TYPES_KSTRING_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SIZE 1024

typedef struct KStringArray KStringArray;
typedef struct KStringArray * KStringArrayPtr;

typedef struct KString {
    uint64_t len;
    uint64_t max;
    char data[MAX_SIZE];
} KString;

typedef KString * KStringPtr;

KString KString__new(const char * str);
KString kstring(const char * str);

bool KString__equals(KStringPtr self, KStringPtr other);
KString KString__sprintf(KStringPtr format, ...);
KString KString__append(KStringPtr self, KStringPtr other);
KString KString__join(KStringArrayPtr array, char delimiter);

KStringArray KString__split(KStringPtr self, char delimiter);
bool KString__starts_with(KStringPtr self, KStringPtr prefix);

const char * KString__cstr(KStringPtr str);

#define EMPTY_KSTRING \
    (KString) { \
        .len = 0, \
        .max = MAX_SIZE, \
        .data[0] = 0 \
    } \

#define cstr(kstr) \
    KString__cstr(&kstr)

#define cstr_p(kstrptr) \
    KString__cstr(kstrptr)


#define STRLEN(s) ( (sizeof(s)/sizeof(s[0])) - sizeof(s[0]) )
#define KSTRING_NEW(chars) \
    (KString) { \
        .max = MAX_SIZE, \
        .len = STRLEN(chars), \
        .data = chars, \
    } \

#define NULL_TERM(ch) \
    (char[2]){ch, '\0'}

#endif
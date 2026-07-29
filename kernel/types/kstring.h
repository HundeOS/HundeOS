#ifndef __KERNEL_TYPES_KSTRING_H
#define __KERNEL_TYPES_KSTRING_H

#include <stdint.h>
#include <stdbool.h>

#include "../../common/types/bstring.h"

#define MAX_SIZE BSTRING_MAX_SIZE

typedef struct KStringArray KStringArray;
typedef struct KStringArray * KStringArrayPtr;

typedef BString KString;
typedef KString * KStringPtr;
typedef const KString * ConstKStringPtr;

OptionalDef(char);

KString KString__new(const char * str);
KString kstring(const char * str);

bool KString__equals(ConstKStringPtr self, ConstKStringPtr other);
KString KString__sprintf(ConstKStringPtr format, ...);
KString KString__append(ConstKStringPtr self, ConstKStringPtr other);
KString KString__join(KStringArrayPtr array, char delimiter);

KStringArray KString__split(ConstKStringPtr self, char delimiter);
bool KString__starts_with(ConstKStringPtr self, ConstKStringPtr prefix);
StringPos KString__copy_to(ConstKStringPtr self, char * target, StringPos target_size);
StringPos KString__length(ConstKStringPtr self);
Optional(char) KString__char_at(ConstKStringPtr self, StringPos index);
KString KString__uppercase(ConstKStringPtr self);

const char * KString__cstr(ConstKStringPtr str);

#define EMPTY_KSTRING \
    EMPTY_BSTRING \

#define cstr(kstr) \
    KString__cstr(&kstr)

#define cstr_p(kstrptr) \
    KString__cstr(kstrptr)


#define STRLEN(s) ( (sizeof(s)/sizeof(s[0])) - sizeof(s[0]) )
#define KSTRING_NEW(chars) \
    BSTRING_NEW(chars) \

#define NULL_TERM(ch) \
    (char[2]){ch, '\0'}

#endif

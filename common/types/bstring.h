#ifndef __COMMON_TYPES_BSTRING_H
#define __COMMON_TYPES_BSTRING_H

#include <stdbool.h>
#include <stdint.h>
#include "optional.h"

#define BSTRING_MAX_SIZE 1024

typedef int64_t StringPos;

typedef struct BString
{
    StringPos len;
    StringPos max;
    char data[BSTRING_MAX_SIZE];
}
BString;

typedef BString * BStringPtr;
typedef const BString * ConstBStringPtr;

OptionalDef(BString);

typedef struct BStringSplit
{
    Optional(BString) match;
    BString rest;
}
BStringSplit;

BString BString__new(const char * str);
BString bstring(const char * str);
void BString__clear(BStringPtr self);
bool BString__is_empty(ConstBStringPtr self);
bool BString__is_whitespace(char ch);
bool BString__push_char(BStringPtr self, char ch);
bool BString__insert_char(BStringPtr self, StringPos index, char ch);
bool BString__remove_at(BStringPtr self, StringPos index);
BString BString__strip(ConstBStringPtr self);
BString BString__slice(ConstBStringPtr self, StringPos start, StringPos end);
BStringSplit BString__split_first_any(ConstBStringPtr self, ConstBStringPtr delimiters);
bool BString__equals(ConstBStringPtr self, ConstBStringPtr other);
bool bstring_equals(void * a, void * b);
BString BString__append(ConstBStringPtr self, ConstBStringPtr other);
bool BString__starts_with(ConstBStringPtr self, ConstBStringPtr prefix);
StringPos BString__copy_to(ConstBStringPtr self, char * target, StringPos target_size);
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

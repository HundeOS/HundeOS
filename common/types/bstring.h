#ifndef __COMMON_TYPES_BSTRING_H
#define __COMMON_TYPES_BSTRING_H

#include <stdbool.h>
#include <stdarg.h>
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
OptionalDef(char);
OptionalDef(uint64_t);

typedef struct BStringSplit
{
    Optional(BString) match;
    BString rest;
}
BStringSplit;

typedef struct BStringArray BStringArray;
typedef struct BStringArray * BStringArrayPtr;

BString BString__new(const char * str);
BString bstring(const char * str);
BString BString__from_chars(const char * chars, StringPos length);
void BString__clear(BStringPtr self);
bool BString__is_empty(ConstBStringPtr self);
bool BString__is_whitespace(char ch);
bool BString__contains_char(ConstBStringPtr self, char ch);
bool BString__push_char(BStringPtr self, char ch);
bool BString__insert_char(BStringPtr self, StringPos index, char ch);
bool BString__set_char_at(BStringPtr self, StringPos index, char ch);
bool BString__remove_at(BStringPtr self, StringPos index);
BString BString__strip(ConstBStringPtr self);
BString BString__slice(ConstBStringPtr self, StringPos start, StringPos end);
BStringSplit BString__split_first_any(ConstBStringPtr self, ConstBStringPtr delimiters);
BStringSplit BString__split_first_token(ConstBStringPtr self, ConstBStringPtr delimiters);
BStringArray BString__split(ConstBStringPtr self, char delimiter);
bool BString__equals(ConstBStringPtr self, ConstBStringPtr other);
bool bstring_equals(void * a, void * b);
BString BString__append(ConstBStringPtr self, ConstBStringPtr other);
BString BString__join(BStringArrayPtr array, char delimiter);
bool BString__starts_with(ConstBStringPtr self, ConstBStringPtr prefix);
StringPos BString__length(ConstBStringPtr self);
Optional(char) BString__char_at(ConstBStringPtr self, StringPos index);
char BString__char_at_or(ConstBStringPtr self, StringPos index, char alternate);
StringPos BString__copy_to(ConstBStringPtr self, char * target, StringPos target_size);
const char * BString__cstr(ConstBStringPtr self);
BString BString__uppercase(ConstBStringPtr self);
Optional(uint64_t) BString__parse_uint64(ConstBStringPtr self);
Optional(uint64_t) BString__parse_uint64_base(ConstBStringPtr self, uint64_t base);
BString BString__vsprintf(ConstBStringPtr self, va_list args);
BString BString__sprintf(ConstBStringPtr self, ...);

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

#define bstr(chars) \
    BSTRING_NEW(chars)

#define empty_bstr() \
    EMPTY_BSTRING

#define cstr(bstr) \
    BString__cstr(&(bstr))

#define cstr_p(bstrptr) \
    BString__cstr(bstrptr)

#define for_each_bstring(self, index, ch, ...) \
    for (StringPos index = 0; index < BString__length(self); index++) \
    { \
        char ch = BString__char_at_or(self, index, '\0'); \
        __VA_ARGS__ \
    }

#endif

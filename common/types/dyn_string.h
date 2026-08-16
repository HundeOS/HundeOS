#ifndef __COMMON_TYPES_DYN_STRING_H
#define __COMMON_TYPES_DYN_STRING_H

#include <stdbool.h>
#include <stddef.h>

#include "bstring.h"
#include "optional.h"
#include "result.h"

typedef enum DynStringCode
{
    DYN_STRING_OK = 0,
    DYN_STRING_ALLOCATION_FAILED = -1,
    DYN_STRING_INVALID_CAPACITY = -2,
} DynStringCode;

ResultDef(DynStringCode);

typedef struct DynString
{
    StringPos len;
    StringPos capacity;
    char * data;
}
DynString;

typedef DynString * DynStringPtr;
typedef const DynString * ConstDynStringPtr;

OptionalDef(DynStringPtr);

Result(DynStringCode) DynString__init(DynStringPtr self, StringPos capacity);
Result(DynStringCode) DynString__reserve(DynStringPtr self, StringPos capacity);
Result(DynStringCode) DynString__push_char(DynStringPtr self, char ch);
Result(DynStringCode) DynString__append_cstr(DynStringPtr self, const char * text);
Result(DynStringCode) DynString__append_bstring(DynStringPtr self, ConstBStringPtr text);
void DynString__clear(DynStringPtr self);
void DynString__dispose(DynStringPtr self);
const char * DynString__cstr(ConstDynStringPtr self);
StringPos DynString__length(ConstDynStringPtr self);
StringPos DynString__capacity(ConstDynStringPtr self);

#endif

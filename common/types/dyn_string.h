#ifndef __COMMON_TYPES_DYN_STRING_H
#define __COMMON_TYPES_DYN_STRING_H

#include <stdbool.h>
#include <stddef.h>

#ifdef DYN_STRING_USE_KERNEL_ALLOC
#include "../../kernel/kalloc.h"
#include "../../kernel/panic.h"
#define DYN_STRING_ALLOC(size) ((void *)Kernel__alloc(size).virtual_addr)
#define DYN_STRING_FREE(ptr)                                                                                              \
    {                                                                                                                     \
        Result(KAllocCode) free_result = Kernel__free(addr(ptr));                                                         \
        match_result(                                                                                                     \
            free_result, success_do_nothing(),                                                                          \
            error(code, message) { Kernel__panic(BString__cstr(&message), NULL); } /* hundelint: ignore-no-null */ \
        );                                                                                                                \
    }
#else
void * malloc(size_t size);
void free(void * ptr);
#define DYN_STRING_ALLOC(size) malloc(size)
#define DYN_STRING_FREE(ptr) free(ptr)
#endif

#ifndef LINKED_LIST_ALLOC
#define LINKED_LIST_ALLOC(size) DYN_STRING_ALLOC(size)
#endif

#ifndef LINKED_LIST_FREE
#define LINKED_LIST_FREE(ptr) DYN_STRING_FREE(ptr)
#endif

#include "bstring.h"
#include "linked_list.h"
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
} DynString;

typedef DynString * DynStringPtr;
typedef const DynString * ConstDynStringPtr;

OptionalDef(DynString);
OptionalDef(DynStringPtr);

bool dyn_string_equals(void * a, void * b);
LinkedListDefine(DynString, DynStringLines, &dyn_string_equals);
typedef DynStringLines * DynStringLinesPtr;
OptionalDef(DynStringLines);
void DynStringLines__dispose_all(DynStringLinesPtr self);

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
Optional(char) DynString__char_at(ConstDynStringPtr self, StringPos index);
char DynString__char_at_or(ConstDynStringPtr self, StringPos index, char alternate);
Optional(BString) DynString__bstring(ConstDynStringPtr self);
Optional(DynStringLines) DynString__lines(ConstDynStringPtr self);

#endif

#ifndef ___KERNEL_TYPES_FIXED_ARRAY_H
#define ___KERNEL_TYPES_FIXED_ARRAY_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "optional.h"
#include "result.h"
#include "types.h"
#include "comparison.h"
#include "../kalloc.h"

extern const char * ARRAY_FULL_ERROR;
extern const char * ARRAY_EMPTY_ERROR;
extern const char * ARRAY_INDEX_OUT_OF_BOUNDS;
extern const char * ITEM_NOT_FOUND;

typedef enum ArrayError
{
    ARRAY_FULL = -1,
    ARRAY_EMPTY = -2,
    ARRAY_OUT_OF_BOUNDS = -3,
    ARRAY_ITEM_NOT_FOUND = -4,
} ArrayError;

typedef int ArrayCode;
typedef int ArrayIndex;

ResultDef(ArrayCode);

ResultDef(ArrayIndex);
OptionalDef(ArrayIndex);

#define _opt_name(alias_name) alias_name##__optional

#define FixedArray(type, size) type##Array_##size

#define ArrayOptional(alias) \
    _Optional(alias##__optional)

#define ConstructorPreDefine(alias_name) \
    alias_name alias_name##__new(); \
    alias_name * alias_name##__heap_new(); \

#define ConstructorDefine(alias_name) \
    alias_name alias_name##__new() \
    { \
        alias_name result = { \
            .len = 0, \
            .max = alias_name##__max_size, \
        }; \
        return result; \
    }; \
    \
    alias_name * alias_name##__heap_new() \
    { \
        alias_name * array = v_ptr(Kernel__alloc(sizeof(alias_name)), alias_name);\
        __d_printf(FOCUS, "Allocated %p\n", array); \
        array->len = 0;\
        array->max = alias_name##__max_size;\
        return array;\
    } \

#define PushPreDefine(alias_name) Result(ArrayCode) alias_name##__push(alias_name * self, alias_name##__type value);
#define PushDefine(alias_name) \
    Result(ArrayCode) alias_name##__push(alias_name * self, alias_name##__type value) \
    { \
        if (self->len >= self->max) \
        { \
            return Error(ArrayCode, KString__new(ARRAY_FULL_ERROR), ARRAY_FULL); \
        } \
        else \
        { \
            ArrayCode index = self->len;\
            self->data[index] = value; \
            self->len++; \
            return Success(ArrayCode, index); \
        } \
    };

#define FAOptional(alias) _Optional(alias##__optional)
#define FAPtrOptional(alias) _Optional(alias##__type_ptr__optional)

#define FAIterate(alias, array, index, value) \
    for (int index = 0; index < array.len; index++) \
    { \
       alias##__type value = array.data[index]; \


#define FAPtrMatch(alias, opt) \
    { \
        typedef alias##__type_ptr value_type; \
        _Optional(alias##__type_ptr) optional = opt; \

#define FindResultMatch(alias, opt) \
    { \
        typedef alias##__FindResult value_type; \
        _Optional(alias##__FindResult) optional = opt; \

#define FANone(alias) None(alias##__optional)
#define FAPtrNone(alias) None(alias##__type_ptr__optional)
#define IsFANone(alias, block) IsNone(alias##__optional, block)
#define IsFAPtrNone(alias, block) IsNone(alias##__type_ptr__optional, block)

#define FASome(alias, value) Some(alias##__optional, value)
#define FAPtrSome(alias, value) Some(alias##__type_ptr__optional, value)
#define IsFASome(alias, value, block) IsSome(alias##__optional, value, block)
#define IsFAPtrSome(alias, value, block) IsSome(alias##__type_ptr__optional, value, block)

#define FindResult(alias) alias##__FindResult
#define FindResultCreate(alias, idx, val) \
    (FindResult(alias)) { .index = idx, .value = val, } \

#define FindResultNone(alias) None(alias##__FindResult)
#define FindResultSome(alias, res) Some(alias##__FindResult, res)

#define FindResultOptional(alias) \
    _Optional(alias##__FindResult) \

#define FindPreDefine(alias_name) \
    typedef bool(* alias_name##__predicate)(alias_name##__type_ptr value, void * context); \
    FindResultOptional(alias_name) alias_name##__find(alias_name * self, alias_name##__predicate predicate, void * context); \
    Optional(ArrayIndex) alias_name##__find_item(alias_name * self, alias_name##__type value); \

#define FindDefine(alias_name) \
    FindResultOptional(alias_name) alias_name##__find(alias_name * self, alias_name##__predicate predicate, void * context) \
    { \
        for (int i = 0; i < self->len; i++) \
        { \
            if (predicate(&self->data[i], context)) { \
                FindResult(alias_name) res = FindResultCreate(alias_name, i, &self->data[i]); \
                return FindResultSome(alias_name, res); \
            }\
        } \
        return FindResultNone(alias_name);\
    };\
    \
    Optional(ArrayIndex) alias_name##__find_item(alias_name * self, alias_name##__type value) \
    { \
        EqualFunction equals = alias_name##__default__equals;\
        for (int i = 0; i < self->len; i++) \
        { \
            if (equals(&self->data[i], &value)) { \
                return Some(ArrayIndex, i); \
            }\
        } \
        return None(ArrayIndex);\
    }; \


#define FilterPreDefine(alias_name) \
alias_name alias_name##__filter(alias_name * self, alias_name##__predicate predicate, void * context); \

#define FilterDefine(alias_name) \
alias_name alias_name##__filter(alias_name * self, alias_name##__predicate predicate, void * context) \
{ \
    alias_name result = alias_name##__new();\
    for (int i = 0; i < self->len; i++) \
    { \
        alias_name##__type_ptr val = &self->data[i];\
        if (predicate(val, context))\
        { \
            alias_name##__push(&result, *val);\
        } \
    } \
    return result;\
} \

#define LenPreDefine(alias_name) size_t alias_name##__len(alias_name * self);

#define LenDefine(alias_name) \
    size_t alias_name##__len(alias_name * self) \
    { \
        return self->len;\
    } \

#define AtPreDefine(alias_name) \
    FAOptional(alias_name) alias_name##__at(alias_name * self, uint64_t index); \
    FAPtrOptional(alias_name) alias_name##__ptr_at(alias_name * self, uint64_t index); \

#define AtDefine(alias_name) \
    FAOptional(alias_name) alias_name##__at(alias_name * self, uint64_t index) \
    { \
        if (index >= self->len) \
        {   \
            return FANone(alias_name); \
        } \
        return FASome(alias_name, self->data[index]); \
    }; \
    FAPtrOptional(alias_name) alias_name##__ptr_at(alias_name * self, uint64_t index) \
    { \
        if (index >= self->len) \
        {   \
            return FAPtrNone(alias_name); \
        } \
        return FAPtrSome(alias_name, &self->data[index]); \
    }; \

#define SetPreDefine(alias_name) Result(ArrayCode) alias_name##__set(alias_name * self, uint64_t index, alias_name##__type value);
#define SetDefine(alias_name) \
    Result(ArrayCode) alias_name##__set(alias_name * self, uint64_t index, alias_name##__type value) \
    { \
        if (index < 0 || index >= self->max) \
        { \
            return Error(ArrayCode, KString__new(ARRAY_INDEX_OUT_OF_BOUNDS), ARRAY_OUT_OF_BOUNDS); \
        } \
        self->data[index] = value;\
        if (index >= self->len)\
        { \
            self->len = index + 1;\
        } \
        \
        return Success(ArrayCode, 0); \
    }; \

#define RemovePreDefine(alias_name) Result(ArrayCode) alias_name##__remove(alias_name * self, alias_name##__type value);
#define RemoveDefine(alias_name) \
    Result(ArrayCode) alias_name##__remove(alias_name * self, alias_name##__type value) \
    { \
        Match(ArrayIndex, alias_name##__find_item(self, value)) \
            IsSome(pos, \
            { \
                for (int i = pos; i < (self->len - 1); i++)\
                { \
                    self->data[i] = self->data[i + 1];\
                } \
                self->len--;\
            })\
            IsNone(\
            { \
                return Error(ArrayCode, kstring(ITEM_NOT_FOUND), ARRAY_ITEM_NOT_FOUND);\
            })\
      End \
      return Success(ArrayCode, 0); \
    } \

#define SortPreDefine(alias_name) \
    typedef int (* alias_name##__Comparator)(alias_name##__type_ptr a, alias_name##__type_ptr b); \
    alias_name alias_name##__sort_desc(alias_name * self, alias_name##__Comparator compare);\
    alias_name alias_name##__sort_asc(alias_name * self, alias_name##__Comparator compare);

#define SortDefine(alias_name) \
    alias_name alias_name##__sort_desc(alias_name * self, alias_name##__Comparator compare) \
    { \
        alias_name result = alias_name##__new(); \
        \
        if (self->len == 0) \
        { \
            return result;\
        } \
        for (int iter = 0; iter < self->len; iter++)\
        { \
            alias_name##__type largest; \
            for (int pos = 0; pos < self->len; pos++) \
            { \
                alias_name##__type val = self->data[pos];\
                if (iter == 0) \
                { \
                    if (pos == 0) \
                    { \
                        largest = val;\
                    } \
                    else \
                    { \
                        if (compare(&val, &largest) > 0) \
                        { \
                            largest = val;\
                        } \
                    } \
                } \
                else \
                { \
                    if (compare(&result.data[iter - 1], &largest) <= 0)\
                    { \
                        largest = val;\
                    } \
                    if (compare(&result.data[iter - 1], &val) > 0)\
                    {\
                        if (compare(&val, &largest) > 0)\
                        { \
                            largest = val; \
                        } \
                    }\
                } \
            End \
            alias_name##__push(&result, largest); \
        } \
        return result;\
    } \
    \
    alias_name alias_name##__sort_asc(alias_name * self, alias_name##__Comparator compare) \
    { \
        alias_name result = alias_name##__new(); \
        if (self->len == 0) \
        { \
            return result;\
        } \
        for (int iter = 0; iter < self->len; iter++)\
        { \
            alias_name##__type smallest; \
            for (int pos = 0; pos < self->len; pos++) \
            { \
                alias_name##__type val = self->data[pos];\
                if (iter == 0) \
                { \
                    if (pos == 0) \
                    { \
                        smallest = val;\
                    } \
                    else \
                    { \
                        if (compare(&val, &smallest) < 0) \
                        { \
                            smallest = val;\
                        } \
                    } \
                } \
                else \
                { \
                    if (compare(&result.data[iter - 1], &smallest) >= 0)\
                    { \
                        smallest = val;\
                    } \
                    if (compare(&result.data[iter - 1], &val) < 0)\
                    {\
                        if (compare(&val, &smallest) < 0)\
                        { \
                            smallest = val; \
                        } \
                    }\
                } \
            End \
            alias_name##__push(&result, smallest); \
        } \
        return result;\
    } \

#define FixedArrayDefine(type, size, alias_name, equals) \
    AliasOptional(type, alias_name##__optional) \
    \
    typedef struct alias_name##__FindResult \
    { \
        type * value; \
        ArrayIndex index; \
    } alias_name##__FindResult; \
    \
    OptionalDef(alias_name##__FindResult);\
    \
    static const uint64_t alias_name##__max_size = size; \
    static const EqualFunction alias_name##__default__equals = equals; \
    \
    typedef type alias_name##__type; \
    typedef type * alias_name##__type_ptr; \
    \
    OptionalDef(alias_name##__type_ptr) \
    AliasOptional(alias_name##__type_ptr, alias_name##__type_ptr__optional) \
    \
    static const type alias_name##__EMPTY; \
    \
    typedef struct alias_name \
    { \
        uint64_t len;\
        uint64_t max; \
        type data[size]; \
    } alias_name; \
    \
    ConstructorPreDefine(alias_name) \
    LenPreDefine(alias_name) \
    AtPreDefine(alias_name) \
    PushPreDefine(alias_name) \
    SetPreDefine(alias_name) \
    FindPreDefine(alias_name) \
    RemovePreDefine(alias_name) \
    SortPreDefine(alias_name) \
    FilterPreDefine(alias_name) \

#define FixedArrayBodyDefine(alias_name) \
    ConstructorDefine(alias_name) \
    LenDefine(alias_name) \
    AtDefine(alias_name) \
    PushDefine(alias_name) \
    SetDefine(alias_name) \
    FindDefine(alias_name) \
    RemoveDefine(alias_name) \
    SortDefine(alias_name) \
    FilterDefine(alias_name) \

#define EmptyArray(alias_name) (alias_name) { \
    .len = 0, \
    .max = alias_name##__max_size, \
} \

#endif
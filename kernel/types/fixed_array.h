#ifndef ___KERNEL_TYPES_FIXED_ARRAY_H
#define ___KERNEL_TYPES_FIXED_ARRAY_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "optional.h"
#include "result.h"
#include "../kalloc.h"

extern const char * ARRAY_FULL_ERROR;
extern const char * ARRAY_EMPTY_ERROR;
extern const char * ARRAY_INDEX_OUT_OF_BOUNDS;

typedef enum ArrayError
{
    E_FULL = -1,
    E_EMPTY = -2,
    E_OUT_OF_BOUNDS = -3,
} ArrayError;

typedef int ResCode;

ResultDef(ResCode);


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
        array->len = 0;\
        array->max = alias_name##__max_size;\
        return array;\
    } \

#define PushPreDefine(alias_name) Result(ResCode) alias_name##__push(alias_name * self, alias_name##__type value);
#define PushDefine(alias_name) \
    Result(ResCode) alias_name##__push(alias_name * self, alias_name##__type value) \
    { \
        if (self->len >= self->max) \
        { \
            return Error(ResCode, KString__new(ARRAY_FULL_ERROR), E_FULL); \
        } \
        else \
        { \
            self->data[self->len++] = value; \
            return Success(ResCode, self->len); \
        } \
    };


#define AtPreDefine(alias_name) FAOptional(alias_name) alias_name##__at(alias_name * self, uint64_t index);
#define AtDefine(alias_name) \
    FAOptional(alias_name) alias_name##__at(alias_name * self, uint64_t index) \
    { \
        if (index >= self->len) \
        { \
            return None(alias_name##__optional); \
        } \
        return Some(alias_name##__optional, self->data[index]); \
    };

#define SetPreDefine(alias_name) Result(ResCode) alias_name##__set(alias_name * self, uint64_t index, alias_name##__type value);
#define SetDefine(alias_name) \
    Result(ResCode) alias_name##__set(alias_name * self, uint64_t index, alias_name##__type value) \
    { \
        if (index < 0 || index >= self->max) \
        { \
            return Error(ResCode, KString__new(ARRAY_INDEX_OUT_OF_BOUNDS), E_OUT_OF_BOUNDS); \
        } \
        self->data[index] = value;\
        if (index >= self->len)\
        { \
            self->len = index + 1;\
        } \
        \
        return Success(ResCode, 0); \
    }; \

#define FAOptional(alias) \
    _Optional(alias##__optional)

#define FixedArrayDefine(type, size, alias_name) \
    AliasOptional(type, alias_name##__optional) \
    static const uint64_t alias_name##__max_size = size; \
    typedef type alias_name##__type; \
    static const type alias_name##__EMPTY; \
    typedef struct FixedArray(type, size) { \
        uint64_t len;\
        uint64_t max; \
        type data[size]; \
    } FixedArray(type, size); \
    typedef FixedArray(type, size) alias_name; \
    ConstructorPreDefine(alias_name) \
    AtPreDefine(alias_name) \
    PushPreDefine(alias_name) \
    SetPreDefine(alias_name) \

#define FixedArrayBodyDefine(alias_name) \
    ConstructorDefine(alias_name) \
    AtDefine(alias_name) \
    PushDefine(alias_name) \
    SetDefine(alias_name) \

#define EmptyArray(alias_name) (alias_name) { \
    .len = 0, \
    .max = alias_name##__max_size, \
} \

#endif
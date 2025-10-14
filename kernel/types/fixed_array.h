#ifndef ___KERNEL_TYPES_FIXED_ARRAY_H
#define ___KERNEL_TYPES_FIXED_ARRAY_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "optional.h"
#include "result.h"

ResultDef(int, IntResult)
extern const char * ARRAY_FULL_ERROR;
extern const char * ARRAY_EMPTY_ERROR;

typedef enum ArrayError
{
    E_FULL = 1,
    E_EMPTY = -1,
} ArrayError;

#define _opt_name(alias_name) alias_name##__optional

#define FixedArray(type, size) type##Array_##size

#define ArrayOptional(alias) \
    _Optional(alias##__optional)

#define ConstructorPreDefine(alias_name) \
    alias_name alias_name##__new();
#define ConstructorDefine(alias_name) \
    alias_name alias_name##__new() \
    { \
        alias_name result = { \
            .len = 0, \
            .max = alias_name##__max_size, \
        }; \
        return result; \
    }; \

#define PushPreDefine(alias_name) IntResult alias_name##__push(alias_name * self, alias_name##__type value);
#define PushDefine(alias_name) \
    IntResult alias_name##__push(alias_name * self, alias_name##__type value) \
    { \
        if (self->len >= self->max) \
        { \
            return Error(IntResult, ARRAY_FULL_ERROR, E_FULL); \
        } \
        else \
        { \
            self->data[self->len++] = value; \
            return Success(IntResult, self->len); \
        } \
    };


#define AtPreDefine(alias_name) ArrayOptional(alias_name) alias_name##__at(alias_name * self, uint64_t index);
#define AtDefine(alias_name) \
    ArrayOptional(alias_name) alias_name##__at(alias_name * self, uint64_t index) \
    { \
        if (index >= self->len) \
        { \
            return None(alias_name##__optional); \
        } \
        return Some(alias_name##__optional, self->data[index]); \
    };

#define Optional(alias) \
    _Optional(alias)

#define FixedArrayDefine(type, size, alias_name) \
    OptionalDef(type, alias_name##__optional) \
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

#define FixedArrayBodyDefine(alias_name) \
    ConstructorDefine(alias_name) \
    AtDefine(alias_name) \
    PushDefine(alias_name)

#define EmptyArray(alias_name) (alias_name) { \
    .len = 0, \
    .max = alias_name##__max_size, \
}

#endif
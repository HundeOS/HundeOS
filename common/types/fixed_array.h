#ifndef __COMMON_TYPES_FIXED_ARRAY_H
#define __COMMON_TYPES_FIXED_ARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bstring.h"
#include "find_result.h"
#include "optional.h"
#include "result.h"
#include "array_common.h"

#ifndef __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
#define __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
typedef bool (*EqualFunction)(void * a, void * b);
#endif

#ifndef FIXED_ARRAY_ALLOC
#error "FIXED_ARRAY_ALLOC(size) must be defined before including common/types/fixed_array.h"
#endif

typedef int ArrayIndex;

ResultDef(ArrayIndex);
OptionalDef(ArrayIndex);

#define _opt_name(alias_name) alias_name##__optional

#define FixedArray(type, size) type##Array_##size

#define ArrayOptional(alias) _Optional(alias##__optional)

#define ConstructorPreDefine(alias_name)                                                                               \
    void alias_name##__init(alias_name * self);                                                                        \
    alias_name alias_name##__new();                                                                                    \
    alias_name * alias_name##__heap_new();

#define InitDefine(alias_name)                                                                                         \
    void alias_name##__init(alias_name * self)                                                                         \
    {                                                                                                                  \
        self->len = 0;                                                                                                 \
    }

#define StackConstructorDefine(alias_name)                                                                             \
    alias_name alias_name##__new()                                                                                     \
    {                                                                                                                  \
        alias_name result;                                                                                             \
        alias_name##__init(&result);                                                                                   \
        return result;                                                                                                 \
    }

#define HeapConstructorDefine(alias_name)                                                                              \
    alias_name * alias_name##__heap_new()                                                                              \
    {                                                                                                                  \
        alias_name * array = (alias_name *)FIXED_ARRAY_ALLOC(sizeof(alias_name));                                      \
        if (array == NULL)                                                                                             \
        {                                                                                                              \
            return NULL;                                                                                               \
        }                                                                                                              \
        alias_name##__init(array);                                                                                     \
        return array;                                                                                                  \
    }

#define ConstructorDefine(alias_name)                                                                                  \
    InitDefine(alias_name) StackConstructorDefine(alias_name) HeapConstructorDefine(alias_name)

#define PushPreDefine(alias_name)                                                                                      \
    Result(CollectionCode) alias_name##__push(alias_name * self, alias_name##__type value);                            \
    Result(CollectionCode) alias_name##__push_all(alias_name * self, alias_name * other);

#define PushDefine(alias_name)                                                                                         \
    Result(CollectionCode) alias_name##__push(alias_name * self, alias_name##__type value)                             \
    {                                                                                                                  \
        if (self->len >= alias_name##__max_size)                                                                       \
        {                                                                                                              \
            return Error(CollectionCode, FIXED_ARRAY_ERROR(ARRAY_FULL_ERROR), ARRAY_FULL);                             \
        }                                                                                                              \
        CollectionCode index = self->len;                                                                              \
        self->data[index] = value;                                                                                     \
        self->len++;                                                                                                   \
        return Success(CollectionCode, index);                                                                         \
    }                                                                                                                  \
                                                                                                                       \
    Result(CollectionCode) alias_name##__push_all(alias_name * self, alias_name * other)                               \
    {                                                                                                                  \
        if (self->len + other->len > alias_name##__max_size)                                                           \
        {                                                                                                              \
            return Error(CollectionCode, FIXED_ARRAY_ERROR(ARRAY_FULL_ERROR), ARRAY_FULL);                             \
        }                                                                                                              \
        CollectionCode first_index = self->len;                                                                        \
        for (uint64_t i = 0; i < other->len; i++)                                                                      \
        {                                                                                                              \
            self->data[self->len] = other->data[i];                                                                    \
            self->len++;                                                                                               \
        }                                                                                                              \
        return Success(CollectionCode, first_index);                                                                   \
    }

#define FAOptional(alias) _Optional(alias##__optional)
#define FAPtrOptional(alias) _Optional(alias##__type_ptr__optional)

#define for_each_fa(alias, array, index, value, ...)                                                                   \
    for (uint64_t index = 0; index < array.len; index++)                                                               \
    {                                                                                                                  \
        alias##__type value = array.data[index];                                                                       \
        __VA_ARGS__                                                                                                    \
    }

#define for_each_fa_ptr(alias, array, index, value, ...)                                                               \
    for (uint64_t index = 0; index < (array)->len; index++)                                                            \
    {                                                                                                                  \
        alias##__type value = (array)->data[index];                                                                    \
        __VA_ARGS__                                                                                                    \
    }

#define FANone(alias) None(alias##__optional)
#define FAPtrNone(alias) None(alias##__type_ptr__optional)
#define IsFANone(alias, block)                                                                                         \
    if (!optional.present)                                                                                             \
    {                                                                                                                  \
        block;                                                                                                         \
    }
#define IsFAPtrNone(alias, block)                                                                                      \
    if (!optional.present)                                                                                             \
    {                                                                                                                  \
        block;                                                                                                         \
    }

#define FASome(alias, value) Some(alias##__optional, value)
#define FAPtrSome(alias, value) Some(alias##__type_ptr__optional, value)
#define IsFASome(alias, value, block)                                                                                  \
    if (optional.present)                                                                                              \
    {                                                                                                                  \
        alias##__type value = optional.some.val;                                                                       \
        block;                                                                                                         \
    }
#define IsFAPtrSome(alias, value, block)                                                                               \
    if (optional.present)                                                                                              \
    {                                                                                                                  \
        alias##__type_ptr value = optional.some.val;                                                                   \
        block;                                                                                                         \
    }

#define FindResult(alias) alias##__FindResult
#define FindResultCreate(alias, idx, val)                                                                              \
    (FindResult(alias))                                                                                                \
    {                                                                                                                  \
        .index = idx, .value = val,                                                                                    \
    }

#define FindResultNone(alias) None(alias##__FindResult)
#define FindResultSome(alias, res) Some(alias##__FindResult, res)

#define FindResultOptional(alias) _Optional(alias##__FindResult)

#define PopPreDefine(alias_name)                                                                                       \
    FAOptional(alias_name) alias_name##__pop(alias_name * self);                                                       \
    FAOptional(alias_name) alias_name##__pop_front(alias_name * self);

#define PopDefine(alias_name)                                                                                          \
    FAOptional(alias_name) alias_name##__pop(alias_name * self)                                                        \
    {                                                                                                                  \
        if (self->len == 0)                                                                                            \
        {                                                                                                              \
            return FANone(alias_name);                                                                                 \
        }                                                                                                              \
        self->len--;                                                                                                   \
        ArrayIndex index = self->len;                                                                                  \
        return FASome(alias_name, self->data[index]);                                                                  \
    }                                                                                                                  \
                                                                                                                       \
    FAOptional(alias_name) alias_name##__pop_front(alias_name * self)                                                  \
    {                                                                                                                  \
        if (self->len == 0)                                                                                            \
        {                                                                                                              \
            return FANone(alias_name);                                                                                 \
        }                                                                                                              \
        FAOptional(alias_name) result = FASome(alias_name, self->data[0]);                                             \
        for (uint64_t i = 1; i < self->len; i++)                                                                       \
        {                                                                                                              \
            self->data[i - 1] = self->data[i];                                                                         \
        }                                                                                                              \
        self->len--;                                                                                                   \
        return result;                                                                                                 \
    }

#define FindPreDefine(alias_name)                                                                                      \
    typedef bool (*alias_name##__predicate)(alias_name##__type_ptr value, void * context);                             \
    FindResultOptional(alias_name)                                                                                     \
        alias_name##__find(alias_name * self, alias_name##__predicate predicate, void * context);                      \
    Optional(ArrayIndex) alias_name##__find_item(alias_name * self, alias_name##__type value);

#define FindDefine(alias_name)                                                                                         \
    FindResultOptional(alias_name)                                                                                     \
        alias_name##__find(alias_name * self, alias_name##__predicate predicate, void * context)                       \
    {                                                                                                                  \
        for (uint64_t i = 0; i < self->len; i++)                                                                       \
        {                                                                                                              \
            if (predicate(&self->data[i], context))                                                                    \
            {                                                                                                          \
                FindResult(alias_name) res = FindResultCreate(alias_name, (ArrayIndex)i, &self->data[i]);              \
                return FindResultSome(alias_name, res);                                                                \
            }                                                                                                          \
        }                                                                                                              \
        return FindResultNone(alias_name);                                                                             \
    }                                                                                                                  \
                                                                                                                       \
    Optional(ArrayIndex) alias_name##__find_item(alias_name * self, alias_name##__type value)                          \
    {                                                                                                                  \
        EqualFunction equals = alias_name##__default__equals;                                                          \
        for (uint64_t i = 0; i < self->len; i++)                                                                       \
        {                                                                                                              \
            if (equals(&self->data[i], &value))                                                                        \
            {                                                                                                          \
                return Some(ArrayIndex, (ArrayIndex)i);                                                                \
            }                                                                                                          \
        }                                                                                                              \
        return None(ArrayIndex);                                                                                       \
    }

#define FilterPreDefine(alias_name)                                                                                    \
    alias_name alias_name##__filter(alias_name * self, alias_name##__predicate predicate, void * context);

#define FilterDefine(alias_name)                                                                                       \
    alias_name alias_name##__filter(alias_name * self, alias_name##__predicate predicate, void * context)              \
    {                                                                                                                  \
        alias_name result = alias_name##__new();                                                                       \
        for (uint64_t i = 0; i < self->len; i++)                                                                       \
        {                                                                                                              \
            alias_name##__type_ptr val = &self->data[i];                                                               \
            if (predicate(val, context))                                                                               \
            {                                                                                                          \
                alias_name##__push(&result, *val);                                                                     \
            }                                                                                                          \
        }                                                                                                              \
        return result;                                                                                                 \
    }

#define LenPreDefine(alias_name) size_t alias_name##__len(alias_name * self);

#define LenDefine(alias_name)                                                                                          \
    size_t alias_name##__len(alias_name * self)                                                                        \
    {                                                                                                                  \
        return self->len;                                                                                              \
    }

#define AtPreDefine(alias_name)                                                                                        \
    FAOptional(alias_name) alias_name##__at(alias_name * self, int64_t index);                                         \
    FAPtrOptional(alias_name) alias_name##__ptr_at(alias_name * self, int64_t index);

#define AtDefine(alias_name)                                                                                           \
    FAOptional(alias_name) alias_name##__at(alias_name * self, int64_t index)                                          \
    {                                                                                                                  \
        if (index < 0)                                                                                                 \
        {                                                                                                              \
            uint64_t from_end = (uint64_t)(-(index + 1)) + 1U;                                                         \
            if (from_end <= self->len)                                                                                 \
            {                                                                                                          \
                index = (int64_t)(self->len - from_end);                                                               \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                return FANone(alias_name);                                                                             \
            }                                                                                                          \
        }                                                                                                              \
        if ((uint64_t)index >= self->len)                                                                              \
        {                                                                                                              \
            return FANone(alias_name);                                                                                 \
        }                                                                                                              \
        return FASome(alias_name, self->data[index]);                                                                  \
    }                                                                                                                  \
    FAPtrOptional(alias_name) alias_name##__ptr_at(alias_name * self, int64_t index)                                   \
    {                                                                                                                  \
        if (index < 0)                                                                                                 \
        {                                                                                                              \
            uint64_t from_end = (uint64_t)(-(index + 1)) + 1U;                                                         \
            if (from_end <= self->len)                                                                                 \
            {                                                                                                          \
                index = (int64_t)(self->len - from_end);                                                               \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                return FAPtrNone(alias_name);                                                                          \
            }                                                                                                          \
        }                                                                                                              \
        if ((uint64_t)index >= self->len)                                                                              \
        {                                                                                                              \
            return FAPtrNone(alias_name);                                                                              \
        }                                                                                                              \
        return FAPtrSome(alias_name, &self->data[index]);                                                              \
    }

#define SetPreDefine(alias_name)                                                                                       \
    Result(CollectionCode) alias_name##__set(alias_name * self, uint64_t index, alias_name##__type value);
#define SetDefine(alias_name)                                                                                          \
    Result(CollectionCode) alias_name##__set(alias_name * self, uint64_t index, alias_name##__type value)              \
    {                                                                                                                  \
        if (index >= alias_name##__max_size)                                                                           \
        {                                                                                                              \
            return Error(CollectionCode, FIXED_ARRAY_ERROR(ARRAY_INDEX_OUT_OF_BOUNDS), ARRAY_OUT_OF_BOUNDS);           \
        }                                                                                                              \
        self->data[index] = value;                                                                                     \
        if (index >= self->len)                                                                                        \
        {                                                                                                              \
            self->len = index + 1;                                                                                     \
        }                                                                                                              \
        return Success(CollectionCode, 0);                                                                             \
    }

#define RemovePreDefine(alias_name)                                                                                    \
    Result(CollectionCode) alias_name##__remove(alias_name * self, alias_name##__type value);
#define RemoveDefine(alias_name)                                                                                       \
    Result(CollectionCode) alias_name##__remove(alias_name * self, alias_name##__type value)                           \
    {                                                                                                                  \
        match_optional(                                                                                                \
            alias_name##__find_item(self, value),                                                                      \
            some(pos) {                                                                                                \
                {                                                                                                      \
                    for (uint64_t i = (uint64_t)pos; i < (self->len - 1); i++)                                         \
                    {                                                                                                  \
                        self->data[i] = self->data[i + 1];                                                             \
                    }                                                                                                  \
                    self->len--;                                                                                       \
                };                                                                                                     \
            },                                                                                                         \
            none() {                                                                                                   \
                {                                                                                                      \
                    return Error(CollectionCode, FIXED_ARRAY_ERROR(ITEM_NOT_FOUND), ARRAY_ITEM_NOT_FOUND);             \
                };                                                                                                     \
            });                                                                                                        \
        return Success(CollectionCode, 0);                                                                             \
    }

#define SortPreDefine(alias_name)                                                                                      \
    typedef int (*alias_name##__Comparator)(alias_name##__type_ptr a, alias_name##__type_ptr b);                       \
    alias_name alias_name##__sort_desc(alias_name * self, alias_name##__Comparator compare);                           \
    alias_name alias_name##__sort_asc(alias_name * self, alias_name##__Comparator compare);

#define SortDefine(alias_name)                                                                                         \
    alias_name alias_name##__sort_desc(alias_name * self, alias_name##__Comparator compare)                            \
    {                                                                                                                  \
        alias_name result = alias_name##__new();                                                                       \
        if (self->len == 0)                                                                                            \
        {                                                                                                              \
            return result;                                                                                             \
        }                                                                                                              \
        for (uint64_t iter = 0; iter < self->len; iter++)                                                              \
        {                                                                                                              \
            alias_name##__type largest;                                                                                \
            for (uint64_t pos = 0; pos < self->len; pos++)                                                             \
            {                                                                                                          \
                alias_name##__type val = self->data[pos];                                                              \
                if (iter == 0)                                                                                         \
                {                                                                                                      \
                    if (pos == 0)                                                                                      \
                    {                                                                                                  \
                        largest = val;                                                                                 \
                    }                                                                                                  \
                    else if (compare(&val, &largest) > 0)                                                              \
                    {                                                                                                  \
                        largest = val;                                                                                 \
                    }                                                                                                  \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    if (compare(&result.data[iter - 1], &largest) <= 0)                                                \
                    {                                                                                                  \
                        largest = val;                                                                                 \
                    }                                                                                                  \
                    if (compare(&result.data[iter - 1], &val) > 0)                                                     \
                    {                                                                                                  \
                        if (compare(&val, &largest) > 0)                                                               \
                        {                                                                                              \
                            largest = val;                                                                             \
                        }                                                                                              \
                    }                                                                                                  \
                }                                                                                                      \
            }                                                                                                          \
            alias_name##__push(&result, largest);                                                                      \
        }                                                                                                              \
        return result;                                                                                                 \
    }                                                                                                                  \
                                                                                                                       \
    alias_name alias_name##__sort_asc(alias_name * self, alias_name##__Comparator compare)                             \
    {                                                                                                                  \
        alias_name result = alias_name##__new();                                                                       \
        if (self->len == 0)                                                                                            \
        {                                                                                                              \
            return result;                                                                                             \
        }                                                                                                              \
        for (uint64_t iter = 0; iter < self->len; iter++)                                                              \
        {                                                                                                              \
            alias_name##__type smallest;                                                                               \
            for (uint64_t pos = 0; pos < self->len; pos++)                                                             \
            {                                                                                                          \
                alias_name##__type val = self->data[pos];                                                              \
                if (iter == 0)                                                                                         \
                {                                                                                                      \
                    if (pos == 0)                                                                                      \
                    {                                                                                                  \
                        smallest = val;                                                                                \
                    }                                                                                                  \
                    else if (compare(&val, &smallest) < 0)                                                             \
                    {                                                                                                  \
                        smallest = val;                                                                                \
                    }                                                                                                  \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    if (compare(&result.data[iter - 1], &smallest) >= 0)                                               \
                    {                                                                                                  \
                        smallest = val;                                                                                \
                    }                                                                                                  \
                    if (compare(&result.data[iter - 1], &val) < 0)                                                     \
                    {                                                                                                  \
                        if (compare(&val, &smallest) < 0)                                                              \
                        {                                                                                              \
                            smallest = val;                                                                            \
                        }                                                                                              \
                    }                                                                                                  \
                }                                                                                                      \
            }                                                                                                          \
            alias_name##__push(&result, smallest);                                                                     \
        }                                                                                                              \
        return result;                                                                                                 \
    }

#define FixedArrayDefine(type, size, alias_name, equals)                                                               \
    AliasOptional(type, alias_name##__optional)                                                                        \
                                                                                                                       \
        typedef struct alias_name##__FindResult                                                                        \
    {                                                                                                                  \
        type * value;                                                                                                  \
        ArrayIndex index;                                                                                              \
    } alias_name##__FindResult;                                                                                        \
                                                                                                                       \
    OptionalDef(alias_name##__FindResult);                                                                             \
                                                                                                                       \
    static const uint64_t alias_name##__max_size = size;                                                               \
    static const EqualFunction alias_name##__default__equals = equals;                                                 \
                                                                                                                       \
    typedef type alias_name##__type;                                                                                   \
    typedef type * alias_name##__type_ptr;                                                                             \
                                                                                                                       \
    OptionalDef(alias_name##__type_ptr) AliasOptional(alias_name##__type_ptr, alias_name##__type_ptr__optional)        \
                                                                                                                       \
        static const type alias_name##__EMPTY;                                                                         \
                                                                                                                       \
    typedef struct alias_name                                                                                          \
    {                                                                                                                  \
        uint64_t len;                                                                                                  \
        type data[size];                                                                                               \
    } alias_name;                                                                                                      \
                                                                                                                       \
    ConstructorPreDefine(alias_name) LenPreDefine(alias_name) AtPreDefine(alias_name) PushPreDefine(alias_name)        \
        PopPreDefine(alias_name) SetPreDefine(alias_name) FindPreDefine(alias_name) RemovePreDefine(alias_name)        \
            SortPreDefine(alias_name) FilterPreDefine(alias_name)

#define FixedArrayBodyDefine(alias_name)                                                                               \
    ConstructorDefine(alias_name) LenDefine(alias_name) AtDefine(alias_name) PushDefine(alias_name)                    \
        PopDefine(alias_name) SetDefine(alias_name) FindDefine(alias_name) RemoveDefine(alias_name)                    \
            SortDefine(alias_name) FilterDefine(alias_name)

#define FixedArrayStackBodyDefine(alias_name)                                                                          \
    InitDefine(alias_name) StackConstructorDefine(alias_name) LenDefine(alias_name) AtDefine(alias_name)               \
        PushDefine(alias_name)                                                                                         \
        PopDefine(alias_name) SetDefine(alias_name) FindDefine(alias_name) RemoveDefine(alias_name)                    \
            SortDefine(alias_name) FilterDefine(alias_name)

#define EmptyArray(alias_name)                                                                                         \
    (alias_name)                                                                                                       \
    {                                                                                                                  \
        .len = 0,                                                                                                      \
    }

#define ArrayInit(alias_name, array) array.len = 0;

#endif

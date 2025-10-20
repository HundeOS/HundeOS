#ifndef __KERNEL_TYPES_MAP_H
#define __KERNEL_TYPES_MAP_H
#include <stdint.h>

#include "comparison.h"
#include "fixed_array.h"

typedef uint8_t MapCode;
ResultDef(MapCode);

extern const char * MAP_FULL_ERROR;

typedef enum MapError
{
    MAP_FULL = -1,
} MapError;


#define EmptyMap(alias) \
    (alias) {\
        .len = 0,\
        .max = alias##__max_size,\
        .equals = alias##__equals_function,\
    }\

#define MapDefine(K, V, size, alias, equals_function) \
    typedef K alias##__key_type;\
    typedef V alias##__value_type;\
    static const int alias##__max_size = size;\
    static const EqualFunction alias##__equals_function = equals_function;\
    \
    AliasOptional(K, alias##__key_type);\
    AliasOptional(V, alias##__value_type);\
    \
    typedef struct alias##__MapEntry\
    { \
        K key; \
        V value;\
    } alias##__MapEntry;\
    \
    typedef alias##__MapEntry * alias##__MapEntryPtr;\
    \
    typedef struct alias \
    { \
        EqualFunction equals;\
        int len;\
        int max;\
        alias##__MapEntry entries[size]; \
    } alias;\
    \
    MapConstructorPreDefine(alias) \
    MapSetPreDefine(alias) \
    MapGetPreDefine(alias) \
    MapDeletePreDefine(alias) \

#define MapBodyDefine(alias) \
    MapConstructorDefine(alias) \
    MapSetDefine(alias) \
    MapGetDefine(alias) \
    MapDeleteDefine(alias) \

#define MapConstructorPreDefine(alias) \
    alias alias##__new();

#define MapConstructorDefine(alias) \
    alias alias##__new() \
    { \
        alias result = { \
            .equals = alias##__equals_function, \
            .len = 0, \
            .max = alias##__max_size, \
        }; \
        return result; \
    }; \

#define MapSetPreDefine(alias) \
    Result(MapCode) alias##__set(alias * self, alias##__key_type key, alias##__value_type value);

#define MapSetDefine(alias) \
    Result(MapCode) alias##__set(alias * self, alias##__key_type key, alias##__value_type value) \
    { \
        alias##__MapEntry entry = (alias##__MapEntry) { \
            .key = key, \
            .value = value,\
        }; \
        int add_index = self->len; \
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i]; \
            if (self->equals(&key, &curr->key))\
            { \
                add_index = i;\
                break; \
            } \
        }\
        if (add_index == self->len)\
        { \
            if (self->len + 1 > self->max)\
            { \
                return Error(MapCode, KString__new(MAP_FULL_ERROR), MAP_FULL);\
            } \
            self->len++;\
        } \
        self->entries[add_index] = entry;\
        return Success(MapCode, add_index);\
    } \

#define MapGetPreDefine(alias) \
    Optional(alias##__value_type) alias##__get(alias * self, alias##__key_type key);

#define MapGetDefine(alias) \
    Optional(alias##__value_type) alias##__get(alias * self, alias##__key_type key) \
    { \
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i];\
            if (self->equals(&key, &curr->key))\
            { \
                return Some(alias##__value_type, curr->value);\
            } \
        }\
        return None(alias##__value_type);\
    } \

#define MapDeletePreDefine(alias) \
    Result(MapCode) alias##__delete(alias * self, alias##__key_type key);

#define MapDeleteDefine(alias) \
    Result(MapCode) alias##__delete(alias * self, alias##__key_type key) \
    { \
        int delete_index = self->len;\
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i];\
            if (self->equals(&key, &curr->key))\
            { \
                delete_index = i;\
                break;\
            } \
        }\
        \
        if (delete_index < self->len) \
        {\
            self->len--;\
            for (int i = delete_index; i < self->len; i++)\
            {\
                self->entries[i] = self->entries[i+1];\
            }\
        }\
        \
        return Success(MapCode, self->len);\
    } \

#endif
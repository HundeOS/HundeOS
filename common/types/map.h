#ifndef __COMMON_TYPES_MAP_H
#define __COMMON_TYPES_MAP_H

#include <stdint.h>

#include "bstring.h"

#ifndef MAP_ERROR
#define MAP_ERROR(message) BString__new(message)
#endif

#define MAP_FULL_ERROR "Map is full"

typedef enum MapError
{
    MAP_FULL = -1,
} MapError;

#define EmptyMap(alias) \
    (alias) {\
        .len = 0,\
    }\

#define MapDefine(K, V, size, alias, equals_function) \
    typedef K alias##__key_type;\
    typedef K * alias##__key_type_ptr;\
    typedef V alias##__value_type;\
    typedef V * alias##__value_type_ptr;\
    static const int alias##__max_size = size;\
    static const EqualFunction alias##__equals_function = equals_function;\
    \
    AliasOptional(K, alias##__key_type);\
    AliasOptional(V, alias##__value_type);\
    OptionalDef(alias##__key_type_ptr);\
    OptionalDef(alias##__value_type_ptr);\
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
        int len;\
        alias##__MapEntry entries[size]; \
    } alias;\
    \
    MapConstructorPreDefine(alias) \
    MapSetPreDefine(alias) \
    MapGetPreDefine(alias) \
    MapDeletePreDefine(alias) \
    MapLenPreDefine(alias) \
    MapKeyPreDefine(alias) \

#define MapBodyDefine(alias) \
    MapConstructorDefine(alias) \
    MapSetDefine(alias) \
    MapGetDefine(alias) \
    MapDeleteDefine(alias) \
    MapLenDefine(alias) \
    MapKeyDefine(alias) \

#define MapInit(alias, map) \
    map.len = 0; \

#define MapConstructorPreDefine(alias) \
    alias alias##__new();

#define MapConstructorDefine(alias) \
    alias alias##__new() \
    { \
        alias result = { \
            .len = 0, \
        }; \
        return result; \
    } \

#define MapSetPreDefine(alias) \
    Result(CollectionCode) alias##__set(alias * self, alias##__key_type key, alias##__value_type value);

#define MapSetDefine(alias) \
    Result(CollectionCode) alias##__set(alias * self, alias##__key_type key, alias##__value_type value) \
    { \
        alias##__MapEntry entry = (alias##__MapEntry) { \
            .key = key, \
            .value = value,\
        }; \
        int add_index = self->len; \
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i]; \
            if (alias##__equals_function(&key, &curr->key))\
            { \
                add_index = i;\
                break; \
            } \
        }\
        if (add_index == self->len)\
        { \
            if (self->len + 1 > alias##__max_size)\
            { \
                return Error(CollectionCode, MAP_ERROR(MAP_FULL_ERROR), MAP_FULL);\
            } \
            self->len++;\
        } \
        self->entries[add_index] = entry;\
        return Success(CollectionCode, add_index);\
    } \

#define MapGetPreDefine(alias) \
    Optional(alias##__value_type) alias##__get(alias * self, alias##__key_type key); \
    Optional(alias##__value_type_ptr) alias##__get_ptr(alias * self, alias##__key_type key); \

#define MapGetDefine(alias) \
    Optional(alias##__value_type) alias##__get(alias * self, alias##__key_type key) \
    { \
        alias##__key_type key_stack = key;\
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i];\
            alias##__key_type curr_key = curr->key;\
            bool match = alias##__equals_function(&key_stack, &curr_key);\
            if (match) \
            { \
                return Some(alias##__value_type, curr->value);\
            } \
        }\
        return None(alias##__value_type);\
    } \
    \
    Optional(alias##__value_type_ptr) alias##__get_ptr(alias * self, alias##__key_type key) \
    { \
        alias##__key_type key_stack = key;\
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i];\
            alias##__key_type curr_key = curr->key;\
            if (alias##__equals_function(&key_stack, &curr_key))\
            { \
                return Some(alias##__value_type_ptr, &curr->value);\
            } \
        }\
        return None(alias##__value_type_ptr);\
    } \

#define MapDeletePreDefine(alias) \
    Result(CollectionCode) alias##__delete(alias * self, alias##__key_type key);

#define MapDeleteDefine(alias) \
    Result(CollectionCode) alias##__delete(alias * self, alias##__key_type key) \
    { \
        int delete_index = self->len;\
        for (int i = 0; i < self->len; i++)\
        {\
            alias##__MapEntryPtr curr = &self->entries[i];\
            if (alias##__equals_function(&key, &curr->key))\
            { \
                delete_index = i;\
                break;\
            } \
        }\
        if (delete_index < self->len) \
        {\
            self->len--;\
            for (int i = delete_index; i < self->len; i++)\
            {\
                self->entries[i] = self->entries[i+1];\
            }\
        }\
        return Success(CollectionCode, self->len);\
    } \

#define MapLenPreDefine(alias) \
    uint64_t alias##__len(alias * self);

#define MapLenDefine(alias) \
    uint64_t alias##__len(alias * self) \
    { \
        return self->len;\
    } \

#define MapKeyPreDefine(alias) \
    Optional(alias##__key_type_ptr) alias##__key(alias * self, uint64_t index);

#define MapKeyDefine(alias) \
    Optional(alias##__key_type_ptr) alias##__key(alias * self, uint64_t index) \
    { \
        if (index >= self->len)\
        { \
            return None(alias##__key_type_ptr);\
        } \
        return Some(alias##__key_type_ptr, &self->entries[index].key);\
    } \

#define for_each_map(alias, map, index, k, v, ...) \
    for (int index = 0; index < map.len; index++) \
    { \
       alias##__key_type k = map.entries[index].key; \
       alias##__value_type v = map.entries[index].value; \
       __VA_ARGS__ \
    }

#define for_each_map_ptr(alias, map, index, k, v, ...) \
    for (int index = 0; index < map->len; index++) \
    { \
       alias##__key_type_ptr k = map->entries[index].key; \
       alias##__value_type_ptr v = map->entries[index].value; \
       __VA_ARGS__ \
    }

#endif

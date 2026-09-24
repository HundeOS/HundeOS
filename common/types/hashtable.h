#ifndef __COMMON_TYPES_HASHTABLE_H
#define __COMMON_TYPES_HASHTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fixed_array.h"
#include "map.h"

#ifndef __COMMON_TYPES_HASH_CODE_DEFINED
#define __COMMON_TYPES_HASH_CODE_DEFINED
typedef uint64_t HashCode;

OptionalDef(HashCode);
ResultDef(HashCode);
#endif

static inline bool HashCode__equals(void * first, void * second)
{
    return *(HashCode *)first == *(HashCode *)second;
}

#define HashtableOptional(alias) _Optional(alias##__V)
#define _HashtableNone(alias) _None(alias##__V)
#define _HashtableSome(alias) _Some(alias##__V)

#define HashtablePtrOptional(alias) _Optional(alias##__V_ptr)
#define _HashtablePtrNone(alias) _None(alias##__V_ptr)
#define _HashtablePtrSome(alias) _Some(alias##__V_ptr)

#define HashtableNone(alias) None(alias##__V)
#define HashtableSome(alias, val) Some(alias##__V, val)

#define HashtablePtrNone(alias) None(alias##__V_ptr)
#define HashtablePtrSome(alias, val) Some(alias##__V_ptr, val)

#define BucketEntryDefine(alias)                                                                                       \
    typedef struct alias##__bucket_entry                                                                               \
    {                                                                                                                  \
        alias##__K key;                                                                                                \
        alias##__V value;                                                                                              \
    } alias##__bucket_entry;

#define BucketEntry(alias, k, v)                                                                                       \
    (alias##__bucket_entry)                                                                                            \
    {                                                                                                                  \
        .key = k, .value = v                                                                                           \
    }

#define HashtableConstructorPreDefine(alias)                                                                           \
    alias alias##__new(void);                                                                                          \
    void alias##__init(alias * self);

#define HashtableConstructorBodyDefine(alias)                                                                          \
    alias alias##__new(void)                                                                                           \
    {                                                                                                                  \
        return (alias){                                                                                                \
            .table = EmptyMap(alias##__table),                                                                         \
            .len = 0,                                                                                                  \
        };                                                                                                             \
    }                                                                                                                  \
                                                                                                                       \
    void alias##__init(alias * self)                                                                                   \
    {                                                                                                                  \
        *self = (alias){                                                                                               \
            .table = EmptyMap(alias##__table),                                                                         \
            .len = 0,                                                                                                  \
        };                                                                                                             \
    }

#define HashtableSetPreDefine(alias)                                                                                   \
    Result(CollectionCode) alias##__set(alias * self, alias##__K key, alias##__V value);

#define HashtableSetBodyDefine(alias)                                                                                  \
    static bool alias##__entry_match(alias##__bucket_entry * entry, void * match_key)                                  \
    {                                                                                                                  \
        return alias##__key_equals(&entry->key, match_key);                                                            \
    }                                                                                                                  \
                                                                                                                       \
    Result(CollectionCode) alias##__set(alias * self, alias##__K key, alias##__V value)                                \
    {                                                                                                                  \
        HashCode hash = alias##__hash(&key) % alias##__table_size;                                                     \
        match_optional(                                                                                                \
            alias##__table__get_ptr(&self->table, hash),                                                               \
            some(bucket) {                                                                                             \
                match_optional(                                                                                        \
                    alias##__bucket__find(bucket, alias##__entry_match, &key),                                         \
                    some(find_result) {                                                                                \
                        ArrayIndex index = find_result.index;                                                          \
                        return alias##__bucket__set(bucket, index, BucketEntry(alias, key, value));                    \
                    },                                                                                                 \
                    none() {                                                                                           \
                        match_result(                                                                                  \
                            alias##__bucket__push(bucket, BucketEntry(alias, key, value)),                             \
                            success(index) {                                                                           \
                                self->len++;                                                                           \
                                return Success(CollectionCode, index);                                                 \
                            },                                                                                         \
                            error(code, message) { return Error(CollectionCode, message, code); });                    \
                    });                                                                                                \
            },                                                                                                         \
            none() {                                                                                                   \
                alias##__bucket bucket = alias##__bucket__new();                                                       \
                match_result(                                                                                          \
                    alias##__bucket__push(&bucket, BucketEntry(alias, key, value)),                                    \
                    success(index) {                                                                                   \
                        match_result(                                                                                  \
                            alias##__table__set(&self->table, hash, bucket),                                           \
                            success(code) {                                                                            \
                                self->len++;                                                                           \
                                return Success(CollectionCode, index);                                                 \
                            },                                                                                         \
                            error(code, message) { return Error(CollectionCode, message, code); });                    \
                    },                                                                                                 \
                    error(code, message) { return Error(CollectionCode, message, code); });                            \
            });                                                                                                        \
        return Error(CollectionCode, BString__new("Hashtable insertion failed"), MAP_FULL);                            \
    }

#define HashtableGetPreDefine(alias)                                                                                   \
    HashtableOptional(alias) alias##__get(alias * self, alias##__K key);                                               \
    HashtablePtrOptional(alias) alias##__get_ptr(alias * self, alias##__K key);

#define HashtableGetBodyDefine(alias)                                                                                  \
    HashtableOptional(alias) alias##__get(alias * self, alias##__K key)                                                \
    {                                                                                                                  \
        HashCode hash = alias##__hash(&key) % alias##__table_size;                                                     \
        match_optional(                                                                                                \
            alias##__table__get_ptr(&self->table, hash),                                                               \
            some(bucket) {                                                                                             \
                match_optional(                                                                                        \
                    alias##__bucket__find(bucket, alias##__entry_match, &key),                                         \
                    some(find_result) { return HashtableSome(alias, find_result.value->value); },                      \
                    none() { return HashtableNone(alias); });                                                          \
            },                                                                                                         \
            none() { return HashtableNone(alias); });                                                                  \
        return HashtableNone(alias);                                                                                   \
    }                                                                                                                  \
                                                                                                                       \
    HashtablePtrOptional(alias) alias##__get_ptr(alias * self, alias##__K key)                                         \
    {                                                                                                                  \
        HashCode hash = alias##__hash(&key) % alias##__table_size;                                                     \
        match_optional(                                                                                                \
            alias##__table__get_ptr(&self->table, hash),                                                               \
            some(bucket) {                                                                                             \
                match_optional(                                                                                        \
                    alias##__bucket__find(bucket, alias##__entry_match, &key),                                         \
                    some(find_result) { return HashtablePtrSome(alias, &find_result.value->value); },                  \
                    none() { return HashtablePtrNone(alias); });                                                       \
            },                                                                                                         \
            none() { return HashtablePtrNone(alias); });                                                               \
        return HashtablePtrNone(alias);                                                                                \
    }

#define HashtableLenPreDefine(alias) uint64_t alias##__len(alias * self);

#define HashtableLenBodyDefine(alias)                                                                                  \
    uint64_t alias##__len(alias * self)                                                                                \
    {                                                                                                                  \
        return self->len;                                                                                              \
    }

#define HashtableDeletePreDefine(alias) Result(CollectionCode) alias##__delete(alias * self, alias##__K key);

#define HashtableDeleteBodyDefine(alias)                                                                               \
    Result(CollectionCode) alias##__delete(alias * self, alias##__K key)                                               \
    {                                                                                                                  \
        HashCode hash = alias##__hash(&key) % alias##__table_size;                                                     \
        match_optional(                                                                                                \
            alias##__table__get_ptr(&self->table, hash),                                                               \
            some(bucket) {                                                                                             \
                match_optional(                                                                                        \
                    alias##__bucket__find(bucket, alias##__entry_match, &key),                                         \
                    some(find_result) {                                                                                \
                        uint64_t index = (uint64_t)find_result.index;                                                  \
                        for (uint64_t next = index + 1; next < bucket->len; next++)                                    \
                        {                                                                                              \
                            bucket->data[next - 1] = bucket->data[next];                                               \
                        }                                                                                              \
                        bucket->len--;                                                                                 \
                        self->len--;                                                                                   \
                        if (bucket->len == 0)                                                                          \
                        {                                                                                              \
                            alias##__table__delete(&self->table, hash);                                                \
                        }                                                                                              \
                    },                                                                                                 \
                    none_do_nothing());                                                                                \
            },                                                                                                         \
            none_do_nothing());                                                                                        \
        return Success(CollectionCode, self->len);                                                                     \
    }

#define HashtableDefine(K, V, table_size, bucket_size, alias, hash_function, key_equals, value_equals)                 \
    typedef K alias##__K;                                                                                              \
    typedef K * alias##__K_ptr;                                                                                        \
    typedef V alias##__V;                                                                                              \
    typedef V * alias##__V_ptr;                                                                                        \
    static const EqualFunction alias##__key_equals = key_equals;                                                       \
    static const size_t alias##__table_size = table_size;                                                              \
                                                                                                                       \
    OptionalDef(alias##__V_ptr)                                                                                        \
                                                                                                                       \
        typedef _Optional(V) HashtableOptional(alias);                                                                 \
    typedef _None(V) _HashtableNone(alias);                                                                            \
    typedef _Some(V) _HashtableSome(alias);                                                                            \
    typedef _Optional(alias##__V_ptr) HashtablePtrOptional(alias);                                                     \
    typedef _None(alias##__V_ptr) _HashtablePtrNone(alias);                                                            \
    typedef _Some(alias##__V_ptr) _HashtablePtrSome(alias);                                                            \
                                                                                                                       \
    BucketEntryDefine(alias) OptionalDef(alias##__bucket_entry) ResultDef(alias##__bucket_entry)                       \
        FixedArrayDefine(alias##__bucket_entry, bucket_size, alias##__bucket, value_equals)                            \
            OptionalDef(alias##__bucket) ResultDef(alias##__bucket) typedef alias##__bucket * alias##BucketPtr;        \
                                                                                                                       \
    MapDefine(HashCode, alias##__bucket, table_size, alias##__table, &HashCode__equals)                                \
                                                                                                                       \
        typedef struct alias                                                                                           \
    {                                                                                                                  \
        alias##__table table;                                                                                          \
        uint64_t len;                                                                                                  \
    } alias;                                                                                                           \
                                                                                                                       \
    typedef HashCode (*alias##__hash_function)(alias##__K_ptr val);                                                    \
    static const alias##__hash_function alias##__hash = hash_function;                                                 \
    HashtableConstructorPreDefine(alias) HashtableSetPreDefine(alias) HashtableGetPreDefine(alias)                     \
        HashtableDeletePreDefine(alias) HashtableLenPreDefine(alias)

#define HashtableBodyDefine(alias)                                                                                     \
    FixedArrayBodyDefine(alias##__bucket) MapBodyDefine(alias##__table) HashtableConstructorBodyDefine(alias)          \
        HashtableSetBodyDefine(alias) HashtableGetBodyDefine(alias) HashtableDeleteBodyDefine(alias)                   \
            HashtableLenBodyDefine(alias)

#define EmptyHashtable(alias)                                                                                          \
    (alias)                                                                                                            \
    {                                                                                                                  \
        .table = EmptyMap(alias##__table), .len = 0,                                                                   \
    }

#define for_each_hashtable(alias, hashtable, bucket_index, entry_index, iter_key, iter_value, ...)                     \
    for (uint64_t bucket_index = 0; bucket_index < (uint64_t)(hashtable).table.len; bucket_index++)                    \
    {                                                                                                                  \
        alias##BucketPtr alias##__current_bucket = &(hashtable).table.entries[bucket_index].value;                     \
        for (uint64_t entry_index = 0; entry_index < alias##__current_bucket->len; entry_index++)                      \
        {                                                                                                              \
            alias##__K iter_key = alias##__current_bucket->data[entry_index].key;                                      \
            alias##__V iter_value = alias##__current_bucket->data[entry_index].value;                                  \
            __VA_ARGS__                                                                                                \
        }                                                                                                              \
    }

#define for_each_hashtable_ptr(alias, hashtable, bucket_index, entry_index, iter_key, iter_value, ...)                 \
    for (uint64_t bucket_index = 0; bucket_index < (uint64_t)(hashtable)->table.len; bucket_index++)                   \
    {                                                                                                                  \
        alias##BucketPtr alias##__current_bucket = &(hashtable)->table.entries[bucket_index].value;                    \
        for (uint64_t entry_index = 0; entry_index < alias##__current_bucket->len; entry_index++)                      \
        {                                                                                                              \
            alias##__K_ptr iter_key = &alias##__current_bucket->data[entry_index].key;                                 \
            alias##__V_ptr iter_value = &alias##__current_bucket->data[entry_index].value;                             \
            __VA_ARGS__                                                                                                \
        }                                                                                                              \
    }

#endif

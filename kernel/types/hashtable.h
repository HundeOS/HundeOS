#ifndef __KERNEL_TYPES_HASHTABLE_H
#define __KERNEL_TYPES_HASHTABLE_H

#include <stdint.h>
#include "common.h"
#include "map.h"
#include "fixed_array.h"


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

#define BucketEntryDefine(alias) \
    typedef struct alias##__bucket_entry \
    { \
        alias##__K key;\
        alias##__V value;\
    } \
    alias##__bucket_entry;\


#define BucketEntry(alias, k, v) \
    (alias##__bucket_entry) { \
        .key = k,\
        .value = v\
    }


#define HashtableConstructorPreDefine(alias) \
    alias alias##__new();

#define HashtableConstructorBodyDefine(alias) \
    alias alias##__new() \
    { \
        return (alias) { \
            .table = EmptyMap(alias##__table),\
        }; \
    }

#define HashtableSetPreDefine(alias) \
    Result(CollectionCode) alias##__set(alias * self, alias##__K key, alias##__V value); \

#define HashtableSetBodyDefine(alias) \
    static bool alias##__entry_match(alias##__bucket_entry * entry, void * match_key)\
    {\
        return alias##__key_equals(&entry->key, match_key);\
    }\
    \
    Result(CollectionCode) alias##__set(alias * self, alias##__K key, alias##__V value) \
    { \
        HashCode hash = alias##__hash(&key) % alias##__table_size;\
        match_optional(alias##__table__value_type_ptr, alias##__table__get_ptr(&self->table, hash), \
            some(bucket) { \
            match_optional(alias##__bucket__FindResult, alias##__bucket__find(bucket, alias##__entry_match, &key), \
                some(find_result) { \
                ArrayIndex index = find_result.index;\
                return alias##__bucket__set(bucket, index, BucketEntry(alias, key, value));\
            }, \
                none() { \
                return alias##__bucket__push(bucket, BucketEntry(alias, key, value));\
            } \
            )\
        }, \
            none() { \
            alias##__bucket bucket = alias##__bucket__new();\
            alias##__bucket__push(&bucket, BucketEntry(alias, key, value));\
            return alias##__table__set(&self->table, hash, bucket);\
        } \
        )\
        return Error(CollectionCode, bstring("no bucket"), 0); \
    } \

#define HashtableGetPreDefine(alias) \
    HashtableOptional(alias) alias##__get(alias * self, alias##__K key); \
    HashtablePtrOptional(alias) alias##__get_ptr(alias * self, alias##__K key); \

#define HashtableGetBodyDefine(alias) \
    HashtableOptional(alias) alias##__get(alias * self, alias##__K key) \
    { \
        HashCode hash = alias##__hash(&key) % alias##__table_size;\
        match_optional(alias##__table__value_type_ptr, alias##__table__get_ptr(&self->table, hash), \
            some(bucket) { \
            match_optional(alias##__bucket__FindResult, alias##__bucket__find(bucket, alias##__entry_match, &key), \
                some(find_result) { \
                return HashtableSome(alias, find_result.value->value); \
            }, \
                none() { return HashtableNone(alias); } \
            )\
        }, \
            none() { return HashtableNone(alias); } \
        )\
        return HashtableNone(alias); \
    } \
    \
    \
    HashtablePtrOptional(alias) alias##__get_ptr(alias * self, alias##__K key) \
    { \
        HashCode hash = alias##__hash(&key) % alias##__table_size;\
        match_optional(alias##__table__value_type_ptr, alias##__table__get_ptr(&self->table, hash), \
            some(bucket) { \
            match_optional(alias##__bucket__FindResult, alias##__bucket__find(bucket, alias##__entry_match, &key), \
                some(find_result) { return HashtablePtrSome(alias, &find_result.value->value); }, \
                none() { return HashtablePtrNone(alias); } \
            )\
        }, \
            none() { return HashtablePtrNone(alias); } \
        )\
        return HashtablePtrNone(alias); \
    } \

#define HashtableDefine(K, V, table_size, bucket_size, alias, hash_function, key_equals, value_equals) \
    typedef K alias##__K;\
    typedef K * alias##__K_ptr;\
    typedef V alias##__V;\
    typedef V * alias##__V_ptr;\
    static const EqualFunction alias##__key_equals = key_equals;\
    static const size_t alias##__table_size = table_size;\
    \
    OptionalDef(alias##__V_ptr)\
    \
    typedef _Optional(V) HashtableOptional(alias);\
    typedef _None(V) _HashtableNone(alias);\
    typedef _Some(V) _HashtableSome(alias);\
    typedef _Optional(alias##__V_ptr) HashtablePtrOptional(alias);\
    typedef _None(alias##__V_ptr) _HashtablePtrNone(alias);\
    typedef _Some(alias##__V_ptr) _HashtablePtrSome(alias);\
    \
    BucketEntryDefine(alias) \
    OptionalDef(alias##__bucket_entry) \
    ResultDef(alias##__bucket_entry) \
    FixedArrayDefine(alias##__bucket_entry, bucket_size, alias##__bucket, value_equals) \
    OptionalDef(alias##__bucket)\
    ResultDef(alias##__bucket)\
    \
    MapDefine(HashCode, alias##__bucket, table_size, alias##__table, key_equals) \
    \
    typedef struct alias \
    { \
        alias##__table table;\
    } alias;\
    \
    typedef HashCode (* alias##__hash_function)(alias##__K_ptr val); \
    static const alias##__hash_function alias##__hash = hash_function; \
    HashtableConstructorPreDefine(alias) \
    HashtableSetPreDefine(alias)\
    HashtableGetPreDefine(alias)\

#define HashtableBodyDefine(alias) \
    FixedArrayBodyDefine(alias##__bucket) \
    MapBodyDefine(alias##__table) \
    HashtableConstructorBodyDefine(alias) \
    HashtableSetBodyDefine(alias)\
    HashtableGetBodyDefine(alias)\


#define EmptyHashtable(alias) \
    (alias) \
    { \
        .table = EmptyMap(alias##__table),\
    } \



#endif

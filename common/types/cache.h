#ifndef HUND_COMMON_TYPES_CACHE_H
#define HUND_COMMON_TYPES_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "optional.h"

typedef enum CacheStoreStatus
{
    CACHE_INSERTED,
    CACHE_UPDATED,
    CACHE_EVICTED,
} CacheStoreStatus;

#define CacheDefine(key_type, value_type, capacity, alias)                                                             \
    typedef key_type alias##__key_type;                                                                                \
    typedef value_type alias##__value_type;                                                                            \
    typedef uint64_t alias##__index_type;                                                                              \
    static const uint64_t alias##__capacity = capacity;                                                                \
    OptionalDef(alias##__value_type);                                                                                  \
    OptionalDef(alias##__index_type);                                                                                  \
                                                                                                                       \
    typedef struct alias##Entry                                                                                        \
    {                                                                                                                  \
        key_type key;                                                                                                  \
        value_type value;                                                                                              \
        uint64_t last_used;                                                                                            \
        bool frequent;                                                                                                 \
    } alias##Entry;                                                                                                    \
                                                                                                                       \
    typedef struct alias                                                                                               \
    {                                                                                                                  \
        uint64_t len;                                                                                                  \
        uint64_t clock;                                                                                                \
        alias##Entry entries[capacity];                                                                                \
    } alias;                                                                                                           \
                                                                                                                       \
    alias alias##__new(void);                                                                                          \
    Optional(alias##__value_type) alias##__get(alias * self, key_type key);                                            \
    CacheStoreStatus alias##__put(alias * self, key_type key, value_type value);                                       \
    bool alias##__replace(alias * self, key_type key, value_type value);                                               \
    bool alias##__invalidate(alias * self, key_type key);                                                              \
    uint64_t alias##__len(alias * self)

#define CacheBodyDefine(alias, keys_equal)                                                                             \
    static Optional(alias##__index_type) alias##__find_index(alias * self, alias##__key_type key)                      \
    {                                                                                                                  \
        for (uint64_t index = 0; index < self->len; index++)                                                           \
        {                                                                                                              \
            if (keys_equal(&self->entries[index].key, &key))                                                           \
            {                                                                                                          \
                return Some(alias##__index_type, index);                                                               \
            }                                                                                                          \
        }                                                                                                              \
        return None(alias##__index_type);                                                                              \
    }                                                                                                                  \
                                                                                                                       \
    static void alias##__touch(alias * self, alias##Entry * entry)                                                     \
    {                                                                                                                  \
        self->clock++;                                                                                                 \
        entry->last_used = self->clock;                                                                                \
        entry->frequent = true;                                                                                        \
    }                                                                                                                  \
                                                                                                                       \
    alias alias##__new(void)                                                                                           \
    {                                                                                                                  \
        return (alias){0};                                                                                             \
    }                                                                                                                  \
                                                                                                                       \
    Optional(alias##__value_type) alias##__get(alias * self, alias##__key_type key)                                    \
    {                                                                                                                  \
        Optional(alias##__index_type) found = alias##__find_index(self, key);                                          \
        if (is_nothing(found))                                                                                         \
        {                                                                                                              \
            return None(alias##__value_type);                                                                          \
        }                                                                                                              \
        uint64_t index = unwrap_optional(found, 0);                                                                    \
        alias##Entry * entry = &self->entries[index];                                                                  \
        alias##__touch(self, entry);                                                                                   \
        return Some(alias##__value_type, entry->value);                                                                \
    }                                                                                                                  \
                                                                                                                       \
    CacheStoreStatus alias##__put(alias * self, alias##__key_type key, alias##__value_type value)                      \
    {                                                                                                                  \
        Optional(alias##__index_type) found = alias##__find_index(self, key);                                          \
        if (!is_nothing(found))                                                                                        \
        {                                                                                                              \
            uint64_t index = unwrap_optional(found, 0);                                                                \
            alias##Entry * entry = &self->entries[index];                                                              \
            entry->value = value;                                                                                      \
            alias##__touch(self, entry);                                                                               \
            return CACHE_UPDATED;                                                                                      \
        }                                                                                                              \
                                                                                                                       \
        self->clock++;                                                                                                 \
        alias##Entry entry = {.key = key, .value = value, .last_used = self->clock};                                   \
        if (self->len < alias##__capacity)                                                                             \
        {                                                                                                              \
            self->entries[self->len] = entry;                                                                          \
            self->len++;                                                                                               \
            return CACHE_INSERTED;                                                                                     \
        }                                                                                                              \
                                                                                                                       \
        uint64_t oldest_index = 0;                                                                                     \
        uint64_t oldest_use = UINT64_MAX;                                                                              \
        bool found_candidate = false;                                                                                  \
        bool found_probationary = false;                                                                               \
        for (uint64_t index = 0; index < self->len; index++)                                                           \
        {                                                                                                              \
            alias##Entry cached = self->entries[index];                                                                \
            bool probationary = !cached.frequent;                                                                      \
            if (!found_candidate || (probationary && !found_probationary) ||                                           \
                (probationary == found_probationary && cached.last_used < oldest_use))                                 \
            {                                                                                                          \
                oldest_index = index;                                                                                  \
                oldest_use = cached.last_used;                                                                         \
                found_candidate = true;                                                                                \
                found_probationary = probationary;                                                                     \
            }                                                                                                          \
        }                                                                                                              \
        self->entries[oldest_index] = entry;                                                                           \
        return CACHE_EVICTED;                                                                                          \
    }                                                                                                                  \
                                                                                                                       \
    bool alias##__replace(alias * self, alias##__key_type key, alias##__value_type value)                              \
    {                                                                                                                  \
        Optional(alias##__index_type) found = alias##__find_index(self, key);                                          \
        if (is_nothing(found))                                                                                         \
        {                                                                                                              \
            return false;                                                                                              \
        }                                                                                                              \
        uint64_t index = unwrap_optional(found, 0);                                                                    \
        alias##Entry * entry = &self->entries[index];                                                                  \
        entry->value = value;                                                                                          \
        alias##__touch(self, entry);                                                                                   \
        return true;                                                                                                   \
    }                                                                                                                  \
                                                                                                                       \
    bool alias##__invalidate(alias * self, alias##__key_type key)                                                      \
    {                                                                                                                  \
        Optional(alias##__index_type) found = alias##__find_index(self, key);                                          \
        if (is_nothing(found))                                                                                         \
        {                                                                                                              \
            return false;                                                                                              \
        }                                                                                                              \
        uint64_t index = unwrap_optional(found, 0);                                                                    \
        for (uint64_t move = index + 1U; move < self->len; move++)                                                     \
        {                                                                                                              \
            self->entries[move - 1U] = self->entries[move];                                                            \
        }                                                                                                              \
        self->len--;                                                                                                   \
        return true;                                                                                                   \
    }                                                                                                                  \
                                                                                                                       \
    uint64_t alias##__len(alias * self)                                                                                \
    {                                                                                                                  \
        return self->len;                                                                                              \
    }

#endif

#ifndef __COMMON_TYPES_ATOMIC_FIXED_ARRAY_H
#define __COMMON_TYPES_ATOMIC_FIXED_ARRAY_H

#include "array_common.h"
#include "optional.h"

// Fixed slots for lock-free scalar values. Zero is a value, not an absent slot.
// Initialize before sharing; all subsequent element access must use this API.
#define AtomicFixedArrayDefine(type, size, alias)                                                                      \
    typedef type alias##__type;                                                                                        \
    OptionalDef(alias##__type);                                                                                        \
    typedef struct alias                                                                                               \
    {                                                                                                                  \
        type data[size];                                                                                               \
    } alias;                                                                                                           \
    typedef alias * alias##Ptr;                                                                                        \
    typedef const alias * Const##alias##Ptr;                                                                           \
    _Static_assert((size) > 0, "Atomic fixed arrays require at least one slot");                                       \
    _Static_assert(__atomic_always_lock_free(sizeof(type), 0), "Atomic fixed array elements must be lock-free");       \
    static inline alias alias##__new(void)                                                                             \
    {                                                                                                                  \
        return (alias){.data = {0}};                                                                                   \
    }                                                                                                                  \
    static inline uint64_t alias##__capacity(Const##alias##Ptr self)                                                   \
    {                                                                                                                  \
        return size;                                                                                                   \
    }                                                                                                                  \
    static inline Optional(alias##__type) alias##__at(Const##alias##Ptr self, uint64_t index)                          \
    {                                                                                                                  \
        if (index >= size)                                                                                             \
        {                                                                                                              \
            return None(alias##__type);                                                                                \
        }                                                                                                              \
        return Some(alias##__type, __atomic_load_n(&self->data[index], __ATOMIC_ACQUIRE));                             \
    }                                                                                                                  \
    static inline Result(CollectionCode) alias##__set(alias##Ptr self, uint64_t index, type value)                     \
    {                                                                                                                  \
        if (index >= size)                                                                                             \
        {                                                                                                              \
            return Error(CollectionCode, FIXED_ARRAY_ERROR(ARRAY_INDEX_OUT_OF_BOUNDS), ARRAY_OUT_OF_BOUNDS);           \
        }                                                                                                              \
        __atomic_store_n(&self->data[index], value, __ATOMIC_RELEASE);                                                 \
        return Success(CollectionCode, 0);                                                                             \
    }                                                                                                                  \
    static inline Optional(alias##__type) alias##__take(alias##Ptr self, uint64_t index)                               \
    {                                                                                                                  \
        if (index >= size)                                                                                             \
        {                                                                                                              \
            return None(alias##__type);                                                                                \
        }                                                                                                              \
        return Some(alias##__type, __atomic_exchange_n(&self->data[index], 0, __ATOMIC_ACQ_REL));                      \
    }

// Each slot is read once on visitation; this is not a snapshot of all slots.
#define for_each_atomic_fa_ptr(alias, array, index, value, ...)                                                        \
    for (uint64_t index = 0; index < alias##__capacity(array); index++)                                                \
    {                                                                                                                  \
        alias##__type value = __atomic_load_n(&(array)->data[index], __ATOMIC_ACQUIRE);                                \
        __VA_ARGS__                                                                                                    \
    }

#endif

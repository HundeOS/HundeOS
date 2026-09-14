#ifndef HUND_ATOMIC_RECORD_ARRAY_H
#define HUND_ATOMIC_RECORD_ARRAY_H
#include "atomic_fixed_array.h"

typedef enum AtomicRecordStatus
{
    ATOMIC_RECORD_INVALID = -1,
    ATOMIC_RECORD_BUSY = -2,
} AtomicRecordStatus;

// Zero before publication. Writers to a slot must be externally serialized.
// Atomic bytes avoid C data races; the generation rejects torn record reads.
#define AtomicRecordArrayDefine(type, capacity, alias)                                                                 \
    AtomicFixedArrayDefine(uint8_t, sizeof(type) * (capacity), alias##Bytes);                                          \
    AtomicFixedArrayDefine(uint64_t, capacity, alias##Generations);                                                    \
    typedef struct alias                                                                                               \
    {                                                                                                                  \
        alias##Bytes bytes;                                                                                            \
        alias##Generations generations;                                                                                \
    } alias;                                                                                                           \
    typedef alias * alias##Ptr;                                                                                        \
    typedef const alias * Const##alias##Ptr;                                                                           \
    static inline Result(type) alias##__at(Const##alias##Ptr self, uint64_t index)                                     \
    {                                                                                                                  \
        if (index >= (capacity))                                                                                       \
        {                                                                                                              \
            return Error(type, bstr("Invalid record index"), ATOMIC_RECORD_INVALID);                                   \
        }                                                                                                              \
        for (unsigned attempt = 0; attempt < 3; attempt++)                                                             \
        {                                                                                                              \
            uint64_t before = unwrap_optional(alias##Generations__at(&self->generations, index), 1);                   \
            if (before & 1)                                                                                            \
            {                                                                                                          \
                continue;                                                                                              \
            }                                                                                                          \
            type value;                                                                                                \
            uint8_t * target = (uint8_t *)&value;                                                                      \
            for (size_t byte = 0; byte < sizeof(type); byte++)                                                         \
            {                                                                                                          \
                target[byte] = unwrap_optional(alias##Bytes__at(&self->bytes, index * sizeof(type) + byte), 0);        \
            }                                                                                                          \
            __atomic_thread_fence(__ATOMIC_ACQ_REL);                                                                   \
            if (before == unwrap_optional(alias##Generations__at(&self->generations, index), 1))                       \
            {                                                                                                          \
                return Success(type, value);                                                                           \
            }                                                                                                          \
        }                                                                                                              \
        return Error(type, bstr("Record changing"), ATOMIC_RECORD_BUSY);                                               \
    }                                                                                                                  \
    static inline Result(CollectionCode) alias##__set(alias##Ptr self, uint64_t index, const type * value)             \
    {                                                                                                                  \
        if (index >= (capacity))                                                                                       \
        {                                                                                                              \
            return Error(CollectionCode, bstr("Invalid record index"), ATOMIC_RECORD_INVALID);                         \
        }                                                                                                              \
        uint64_t generation = unwrap_optional(alias##Generations__at(&self->generations, index), 0);                   \
        alias##Generations__set(&self->generations, index, generation + 1);                                            \
        __atomic_thread_fence(__ATOMIC_SEQ_CST);                                                                       \
        const uint8_t * source = (const uint8_t *)value;                                                               \
        for (size_t byte = 0; byte < sizeof(type); byte++)                                                             \
        {                                                                                                              \
            alias##Bytes__set(&self->bytes, index * sizeof(type) + byte, source[byte]);                                \
        }                                                                                                              \
        alias##Generations__set(&self->generations, index, generation + 2);                                            \
        return Success(CollectionCode, 0);                                                                             \
    }
#endif

#ifndef ___KERNEL_TYPES_FIXED_ARRAY_H
#define ___KERNEL_TYPES_FIXED_ARRAY_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../panic.h"

typedef struct OptionalV {
    bool present;
    void * value;
} OptionalV;

inline OptionalV Some(void * value)
{
    return (OptionalV) {
        .present = true,
        .value = value
    };
}

inline OptionalV None()
{
    return (OptionalV) {
        .present = false,
        .value = NULL
    };
}

#define something true
#define nothing false

#define unwrap(type, self, a, b) \
    switch (self.present) { \
        case something: a; break\
        case nothing: b; break;\
    }

#define unwrapSome(type, self) \
    type * ___unwrapped = (type) self.value; \
    if (self.present) \

#define unwrapNone(self) \
    if (!self.present) \



#define FixedArray(type, size) type##Array_##size
#define Optional(type) type##Optional

#define OptionalDefine(type) \
    typedef struct T##Optional(type) \
    { \
        bool present; \
        type value; \
    } T##Optional(type); \
    typedef union Optional(type) \
    { \
        T##Optional(type) typed, \
        OptionalV untyped \
    } Optional(type);
//            .max = alias_name##__max_size, \

#include "../print.h"
static inline uint64_t rdsp(void){ uint64_t sp; __asm__ volatile("mov %0, sp":"=r"(sp)); return sp; }
// alias_name result = (alias_name) { \
//     .len = 0, \
//     .max = 128 \
// }; \

#define ConstructorDefine(alias_name) \
    alias_name alias_name##__new() \
    { \
        k_printf("in new sp=%p\n", rdsp());\
        alias_name result = { \
            .len = 0, \
            .max = alias_name##__max_size, \
        }; \
        k_printf("in new returning %i\n", alias_name##__max_size); \
        return result; \
    }; \

#define ConstructorPreDefine(alias_name) alias_name alias_name##__new();

#define PushDefine(alias_name) \
    void alias_name##__push(alias_name * self, alias_name##__type value) \
    { \
        if (self->len >= self->max) \
        { \
            Kernel__panic("Array is full", NULL); \
        } \
        else \
        { \
            self->data[self->len++] = value; \
        } \
    };
#define PushPreDefine(alias_name) void alias_name##__push(alias_name * self, alias_name##__type value);

#define AtDefine(alias_name) \
    alias_name##__type alias_name##__at(alias_name * self, uint64_t index) \
    { \
        if (index >= self->len || index < 0) \
        { \
            return alias_name##__EMPTY; \
        } \
        return self->data[index]; \
    };
#define AtPreDefine(alias_name) alias_name##__type alias_name##__at(alias_name * self, uint64_t index);

#define FixedArrayDefine(type, size, alias_name) \
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
    PushPreDefine(alias_name)

#define FixedArrayBodyDefine(alias_name) \
    ConstructorDefine(alias_name) \
    AtDefine(alias_name) \
    PushDefine(alias_name)

#define EmptyArray(alias_name) (alias_name) { \
    .len = 0, \
    .max = alias_name##__max_size, \
}

#endif
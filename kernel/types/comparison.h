#ifndef __KERNEL_TYPES_COMPARISON_H
#define __KERNEL_TYPES_COMPARISON_H

#include <stdbool.h>
#include <stdint.h>

typedef bool(* EqualFunction)(void * a, void * b);

#define SimpleEquals(T) \
    T##_equals

#define SimpleEqualsPreDef(T) \
    bool T##_equals(void * a, void * b);

#define SimpleEqualsBody(T) \
    bool T##_equals(void * a, void * b) \
    { \
        return *(T *)a == *(T *)b; \
    }

SimpleEqualsPreDef(int)
SimpleEqualsPreDef(bool)
SimpleEqualsPreDef(uint64_t)
SimpleEqualsPreDef(uint32_t)
SimpleEqualsPreDef(uint16_t)
SimpleEqualsPreDef(uint8_t)
SimpleEqualsPreDef(int64_t)
SimpleEqualsPreDef(int32_t)
SimpleEqualsPreDef(int16_t)
SimpleEqualsPreDef(int8_t)
SimpleEqualsPreDef(char)

bool ptr_equals(void * a, void * b);
bool kstring_equals(void * a, void * b);


#endif
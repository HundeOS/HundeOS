#ifndef __KERNEL_TYPES_COMPARISON_H
#define __KERNEL_TYPES_COMPARISON_H

#include <stdbool.h>
#include <stdint.h>
#include "common.h"

#ifndef __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
#define __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
typedef bool(* EqualFunction)(void * a, void * b);
#endif

#define SimpleEquals(T) \
    T##_equals

#define SimpleEqualsWrapper(E) \
    E##__wrap

#define SimpleEqualsWrapperDef(E) \
    bool E##__wrap(void * a, void * b); \

#define SimpleEqualsWrapperBody(T, E) \
    bool E##__wrap(void * a, void * b) \
    { \
        E((T) a, (T) b);\
    } \


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
SimpleEqualsPreDef(Char)

bool Ptr__equals(void * a, void * b);
bool bstring_equals(void * a, void * b);

bool never__equals(void * a, void * b);

HashCode hash__uint64(uint64_t * key);


#endif

#ifndef __KERNEL_TYPES_H
#define __KERNEL_TYPES_H
#include <stdint.h>
#include "result.h"
#include "optional.h"
#include "fixed_array.h"
#include "kstring.h"

typedef uint8_t Char;
typedef Char * CharPtr;
OptionalDef(Char);
ResultDef(Char);

typedef uint64_t Anything;

inline const char * str(Anything a)
{
    return (const char *)a;
}

ResultDef(Anything);

#endif
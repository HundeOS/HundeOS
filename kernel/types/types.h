#ifndef __KERNEL_TYPES_H
#define __KERNEL_TYPES_H
#include <stdint.h>
#include "result.h"
#include "optional.h"
#include "bstring.h"

typedef uint8_t Data;
typedef Data * DataPtr;

typedef uint8_t Char;
typedef Char * CharPtr;

typedef void * NoType;

#define Nothing ((NoType)0)

OptionalDef(NoType);
ResultDef(NoType);

OptionalDef(Char);
ResultDef(Char);

typedef uint64_t Anything;

inline const char * str(Anything a)
{
    return (const char *)a;
}

ResultDef(Anything);

#endif

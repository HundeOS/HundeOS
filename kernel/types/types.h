#ifndef __KERNEL_TYPES_H
#define __KERNEL_TYPES_H
#include <stdint.h>
#include "result.h"

typedef uint64_t Anything;

inline const char * str(Anything a)
{
    return (const char *)a;
}

ResultDef(Anything);

#endif
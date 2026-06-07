#ifndef __KERNEL_TYPES_COMMON_H
#define __KERNEL_TYPES_COMMON_H

#include <stdint.h>
#include "result.h"
#include "optional.h"

typedef uint64_t HashCode;

OptionalDef(HashCode);
ResultDef(HashCode);

typedef uint8_t CollectionCode;
ResultDef(CollectionCode);


#endif
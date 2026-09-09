#ifndef __KERNEL_TYPES_COMMON_H
#define __KERNEL_TYPES_COMMON_H

#include <stdint.h>
#include "result.h"
#include "optional.h"

typedef uint64_t HashCode;

OptionalDef(HashCode);
ResultDef(HashCode);

#ifndef __COMMON_TYPES_COLLECTION_CODE_DEFINED
#define __COMMON_TYPES_COLLECTION_CODE_DEFINED
typedef uint8_t CollectionCode;
ResultDef(CollectionCode);
#endif

#endif

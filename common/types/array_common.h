#ifndef __COMMON_TYPES_ARRAY_COMMON_H
#define __COMMON_TYPES_ARRAY_COMMON_H

#include "result.h"

#ifndef __COMMON_TYPES_COLLECTION_CODE_DEFINED
#define __COMMON_TYPES_COLLECTION_CODE_DEFINED
typedef uint8_t CollectionCode;
ResultDef(CollectionCode);
#endif

#ifndef FIXED_ARRAY_ERROR
#define FIXED_ARRAY_ERROR(message) BString__new(message)
#endif

#define ARRAY_FULL_ERROR "Array is full"
#define ARRAY_EMPTY_ERROR "Array is empty"
#define ARRAY_INDEX_OUT_OF_BOUNDS "Array index is out of bounds"
#define ITEM_NOT_FOUND "Item not found in array"

typedef enum ArrayError
{
    ARRAY_FULL = -1,
    ARRAY_EMPTY = -2,
    ARRAY_OUT_OF_BOUNDS = -3,
    ARRAY_ITEM_NOT_FOUND = -4,
} ArrayError;

#endif

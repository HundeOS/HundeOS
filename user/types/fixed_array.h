#ifndef __USER_TYPES_FIXED_ARRAY_H
#define __USER_TYPES_FIXED_ARRAY_H

#include "../../common/types/result.h"
#include "../../user/user.h"

#ifndef __COMMON_TYPES_COLLECTION_CODE_DEFINED
#define __COMMON_TYPES_COLLECTION_CODE_DEFINED
typedef uint8_t CollectionCode;
ResultDef(CollectionCode);
#endif

#ifndef __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
#define __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
typedef bool(* EqualFunction)(void * a, void * b);
#endif

#define FIXED_ARRAY_ALLOC(size) malloc(size)
#define FIXED_ARRAY_ERROR(message) BString__new(message)

#include "../../common/types/fixed_array.h"

#endif

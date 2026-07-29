#ifndef __USER_TYPES_LINKED_LIST_H
#define __USER_TYPES_LINKED_LIST_H

#include <stdbool.h>
#include <stdint.h>

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

#define LINKED_LIST_ALLOC(size) malloc(size)
#define LINKED_LIST_FREE(ptr) free(ptr)

#include "../../common/types/linked_list.h"

#endif

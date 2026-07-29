#ifndef __TEST_SHARED_TYPES_SHARED_LINKED_LIST_H
#define __TEST_SHARED_TYPES_SHARED_LINKED_LIST_H

#include <stdlib.h>

#define LINKED_LIST_ALLOC(size) malloc(size)
#define LINKED_LIST_FREE(ptr) free(ptr)

#include "../../../common/types/linked_list.h"

#endif

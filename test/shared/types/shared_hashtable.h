#ifndef HUND_TEST_SHARED_HASHTABLE_H
#define HUND_TEST_SHARED_HASHTABLE_H

#include <stdlib.h>

#define FIXED_ARRAY_ALLOC(size) malloc(size)
#define FIXED_ARRAY_ERROR(message) BString__new(message)

#include "../../../common/types/hashtable.h"

#endif

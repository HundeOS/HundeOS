#ifndef __TEST_SHARED_TYPES_SHARED_FIXED_ARRAY_H
#define __TEST_SHARED_TYPES_SHARED_FIXED_ARRAY_H

#include <stdlib.h>

#define FIXED_ARRAY_ALLOC(size) malloc(size)
#define FIXED_ARRAY_ERROR(message) BString__new(message)

#include "../../../common/types/fixed_array.h"

#endif

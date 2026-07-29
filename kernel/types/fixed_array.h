#ifndef ___KERNEL_TYPES_FIXED_ARRAY_H
#define ___KERNEL_TYPES_FIXED_ARRAY_H

#include "../address.h"
#include "common.h"
#include "comparison.h"
#include "optional.h"
#include "result.h"
#include "types.h"

KernelAddress Kernel__alloc(size_t size);

#define FIXED_ARRAY_ALLOC(size) ((void *) Kernel__alloc(size).virtual_addr)
#define FIXED_ARRAY_ERROR(message) BString__new(message)

#include "../../common/types/fixed_array.h"

#endif

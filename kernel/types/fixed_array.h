#ifndef ___KERNEL_TYPES_FIXED_ARRAY_H
#define ___KERNEL_TYPES_FIXED_ARRAY_H

#include "../address.h"
#include "common.h"
#include "comparison.h"
#include "optional.h"
#include "result.h"
#include "types.h"

#ifdef Kernel__alloc
#define KERNEL_TYPES_FIXED_ARRAY_RESTORE_KERNEL_ALLOC
#undef Kernel__alloc
#endif
KernelAddress Kernel__alloc(size_t size);
#ifdef KERNEL_TYPES_FIXED_ARRAY_RESTORE_KERNEL_ALLOC
#define Kernel__alloc(size) Kernel__alloc_debug((size), KALLOC_SOURCE)
#undef KERNEL_TYPES_FIXED_ARRAY_RESTORE_KERNEL_ALLOC
#endif

#define FIXED_ARRAY_ALLOC(size) ((void *) Kernel__alloc(size).virtual_addr)
#define FIXED_ARRAY_ERROR(message) BString__new(message)

#include "../../common/types/fixed_array.h"

#endif

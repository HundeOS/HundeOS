#ifndef __KERNEL_KMALLOC_H
#define __KERNEL_KMALLOC_H

#include <stddef.h>

#include "kalloc.h"

KernelAddress Kernel__malloc(size_t size);
Result(KAllocCode) Kernel__free_malloc(Address allocation);

#endif

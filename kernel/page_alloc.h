#ifndef __KERNEL_PAGE_ALLOC_H_
#define __KERNEL_PAGE_ALLOC_H_

#include <stddef.h>
#include "address.h"
#include "all_defs.h"

typedef struct MemoryPage {
    char page[PAGE_SIZE];
} MemoryPage;

KernelAddress MemoryPage__alloc();
KernelAddress MemoryPage__alloc_size(size_t size);

void MemoryPage__free(MemoryPage * page);

KernelAddress MemoryPage__from_physical_address(Address physical);
KernelAddress MemoryPage__from_virtual_address(Address virtual);
MemoryPage * MemoryPage__zero(MemoryPage * self);

#endif
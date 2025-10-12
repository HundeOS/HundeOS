#ifndef __KERN_KALLOC_H__
#define __KERN_KALLOC_H__
#include <stddef.h>

#include "page_alloc.h"

Address Kernel__alloc_kernel(size_t size);
UserAddress Kernel__alloc_user(Address virtual_address,
                               size_t size,
                               bool read,
                               bool write,
                               bool execute);
KernelAddress Kernel__alloc(size_t size);
void Kernel__memcopy(Address src, Address target, size_t size);

#endif
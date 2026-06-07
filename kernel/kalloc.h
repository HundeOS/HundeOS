#ifndef __KERN_KALLOC_H__
#define __KERN_KALLOC_H__
#include <stddef.h>
#include <stdint.h>
#include "address.h"
#include "types/result.h"
#include "heap.h"

typedef uint64_t KAllocCode;
ResultDef(KAllocCode);

typedef enum KAllocCodes {
    KALLOC_OK = 0,
    KALLOC_NOT_IMPLEMENTED = -1,
} KAllocCodes;

Address Kernel__alloc_kernel(size_t size);
UserAddress Kernel__alloc_user(Address virtual_address,
                               size_t size,
                               bool read,
                               bool write,
                               bool execute);
KernelAddress Kernel__alloc(size_t size);
KernelAddress Kernel__from_physical_address(Address physical);
KernelAddress Kernel__from_virtual_address(Address virtual_addr);

Result(KAllocCode) Kernel__free(Address virtual_addr);

void Kernel__memcopy(Address src, Address target, size_t size);

#endif
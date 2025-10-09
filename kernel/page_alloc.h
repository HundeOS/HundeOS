#ifndef __KERNEL_PAGE_ALLOC_H_
#define __KERNEL_PAGE_ALLOC_H_

#include <stdint.h>
#include <stdbool.h>
#include "mem.h"

typedef uint64_t Address;
typedef uint64_t Address;

#define addr_of(p) addr(&p)

inline Address addr(void * p) {
    return (Address)p;
}

typedef struct KernelAddress {
    Address virtual_addr;
    Address phys_addr;
    bool valid;
} KernelAddress;

typedef struct MemoryPage {
    char page[PAGE_SIZE];
} MemoryPage;


MemoryPage * MemoryPage__alloc();
void MemoryPage__free(MemoryPage * page);

KernelAddress MemoryPage__physical_address(MemoryPage * self);
MemoryPage * MemoryPage__zero(MemoryPage * self);
KernelAddress MemoryPage__address(MemoryPage * self);

#endif
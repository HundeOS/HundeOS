#ifndef __KERNEL_ARCH_PAGE_TABLE_H_
#define __KERNEL_ARCH_PAGE_TABLE_H_
#include "../address.h"
#include <stddef.h>

typedef struct PageTable PageTable;
typedef PageTable * PageTablePtr;
typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

extern KernelAddress PageTable__create();
extern KernelAddress PageTable__kernel_address(PageTablePtr self);
extern KernelAddress PageTable__new(KernelPtr kernel);
extern void PageTable__map(PageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void PageTable__map_mmio(PageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void PageTable__unmap(PageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size);

#endif

#ifndef __KERNEL_ARCH_PAGE_TABLE_H_
#define __KERNEL_ARCH_PAGE_TABLE_H_
#include "../address.h"
#include <stddef.h>

typedef struct PageTable PageTable;
typedef PageTable * PageTablePtr;

extern KernelAddress PageTable__create();
extern KernelAddress PageTable__kernel_address(PageTablePtr self);
extern KernelAddress PageTable__new();
extern void PageTable__map(PageTablePtr self, UserAddress address, size_t size);
extern void PageTable__unmap(PageTablePtr self, Address virtual_address, size_t size);

#endif

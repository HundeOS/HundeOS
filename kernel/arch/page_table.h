#ifndef __KERNEL_ARCH_PAGE_TABLE_H_
#define __KERNEL_ARCH_PAGE_TABLE_H_
#include "../address.h"

typedef struct PageTable PageTable;

extern KernelAddress PageTable__create();
extern KernelAddress PageTable__kernel_address(PageTable * self);
extern KernelAddress PageTable__new();
extern void PageTable__map(PageTable * self, UserAddress address, size_t size);

#endif
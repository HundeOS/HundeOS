#ifndef __KERNEL_ARCH_PAGE_TABLE_H_
#define __KERNEL_ARCH_PAGE_TABLE_H_
#include "../address.h"
#include "../kalloc.h"
#include "../types/linked_list.h"
#include <stddef.h>

typedef struct PageTable PageTable;
typedef PageTable * PageTablePtr;
typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

bool ProcessPageTablePage__equals(KernelAddressPtr self, KernelAddressPtr other);
OptionalDef(KernelAddress);
LinkedListDefine(KernelAddress, ProcessPageTablePages, (EqualFunction)ProcessPageTablePage__equals);

typedef struct ProcessPageTable
{
    KernelAddress root;
    ProcessPageTablePages pages;
} ProcessPageTable;

typedef ProcessPageTable * ProcessPageTablePtr;
typedef const ProcessPageTable * ConstProcessPageTablePtr;

ResultDef(ProcessPageTable);

extern KernelAddress PageTable__create();
extern KernelAddress PageTable__kernel_address(PageTablePtr self);
extern Result(ProcessPageTable) ProcessPageTable__new(KernelPtr kernel);
extern KernelAddress ProcessPageTable__root(ProcessPageTablePtr self);
extern PageTablePtr ProcessPageTable__root_table(ProcessPageTablePtr self);
extern size_t ProcessPageTable__page_count(ProcessPageTablePtr self);
extern Result(KAllocCode) ProcessPageTable__dispose(ProcessPageTablePtr self, KernelPtr kernel);
extern Result(KAllocCode) ArchProcessPageTable__dispose(ProcessPageTablePtr self);
extern void ArchPageTable__map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
Result(KAllocCode) ArchPageTable__try_map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
Result(KAllocCode)
    PageTable__try_map_shared(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void ArchPageTable__map_mmio(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void ArchPageTable__unmap(ProcessPageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size);
extern void PageTable__map_shared(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void PageTable__map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void PageTable__map_mmio(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size);
extern void PageTable__unmap(ProcessPageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size);

#endif

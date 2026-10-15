#include "kernel_core.h"
#include "arch/page_table.h"

LinkedListBodyDefine(ProcessPageTablePages);

bool ProcessPageTablePage__equals(KernelAddressPtr self, KernelAddressPtr other)
{
    return self->valid == other->valid && self->virtual_addr == other->virtual_addr &&
           self->phys_addr == other->phys_addr;
}

Result(KAllocCode) ProcessPageTable__record_page(ProcessPageTablePtr self, KernelAddress page)
{
    match_result(
        ProcessPageTablePages__push(&self->pages, page),
        success(_)
        {
            return Success(KAllocCode, KALLOC_OK);
        },
        error(code, message)
        {
            return Error(KAllocCode, message, KALLOC_ALLOCATION_FAILED);
        }
    );

    return Error(KAllocCode, bstr("Page table allocation failed"), KALLOC_ALLOCATION_FAILED);
}

KernelAddress ProcessPageTable__root(ProcessPageTablePtr self)
{
    return self->root;
}

PageTablePtr ProcessPageTable__root_table(ProcessPageTablePtr self)
{
    return v_ptr(ProcessPageTable__root(self), PageTable);
}

size_t ProcessPageTable__page_count(ProcessPageTablePtr self)
{
    return ProcessPageTablePages__len(&self->pages);
}

static void PageTable__account(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size,
                               MemoryMappingKind kind)
{
    Address start = address.virtual_addr & ~(PAGE_SIZE - 1ULL);
    Address end = (address.virtual_addr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1ULL);
    if (size == 0)
    {
        return;
    }
    MemoryMapping mapping =
        MemoryMapping__new(ProcessPageTable__root(self).virtual_addr, start,
                           address.kernel_addr.virtual_addr - (address.virtual_addr - start), end - start, kind);
    MemoryAccounting__map(&kernel->memory_accounting, &mapping);
}

void PageTable__map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    ArchPageTable__map(self, kernel, address, size);
    PageTable__account(self, kernel, address, size, MEMORY_PRIVATE);
}

void PageTable__map_shared(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    ArchPageTable__map(self, kernel, address, size);
    PageTable__account(self, kernel, address, size, MEMORY_SHARED);
}

Result(KAllocCode)
    PageTable__try_map_shared(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    match_result(
        ArchPageTable__try_map(self, kernel, address, size),
        success(code)
        {
            PageTable__account(self, kernel, address, size, MEMORY_SHARED);
            return Success(KAllocCode, code);
        },
        error(code, message)
        {
            return Error(KAllocCode, message, code);
        }
    );
}

Result(KAllocCode)
    PageTable__try_map_shared_blocks(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    match_result(
        ArchPageTable__try_map_blocks(self, kernel, address, size),
        success(code)
        {
            PageTable__account(self, kernel, address, size, MEMORY_SHARED);
            return Success(KAllocCode, code);
        },
        error(code, message)
        {
            return Error(KAllocCode, message, code);
        }
    );
}

void PageTable__map_mmio(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    ArchPageTable__map_mmio(self, kernel, address, size);
    PageTable__account(self, kernel, address, size, MEMORY_DEVICE);
}

void PageTable__unmap(ProcessPageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size)
{
    if (size == 0)
    {
        return;
    }
    ArchPageTable__unmap(self, kernel, virtual_address, size);
    Address start = virtual_address & ~(PAGE_SIZE - 1ULL);
    Address end = (virtual_address + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1ULL);
    MemoryAccounting__unmap(&kernel->memory_accounting, ProcessPageTable__root(self).virtual_addr, start, end - start);
}

Result(KAllocCode) ProcessPageTable__dispose(ProcessPageTablePtr self, KernelPtr kernel)
{
    Address space = ProcessPageTable__root(self).virtual_addr;

    for_each_ll(
        ProcessPageTablePages,
        self->pages,
        page,
        {
            match_result(
                Kernel__free(page.virtual_addr),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(KAllocCode, message, code);
                }
            );
        }
    );

    match_result(
        ProcessPageTablePages__dispose(&self->pages),
        success(code)
        {
            self->root = NULL_KERNEL_ADDRESS;
            MemoryAccounting__forget(&kernel->memory_accounting, space);
            return Success(KAllocCode, code);
        },
        error(code, message)
        {
            return Error(KAllocCode, message, code);
        }
    );
}

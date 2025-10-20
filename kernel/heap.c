#include "heap.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "memory_page.h"

#include <stdint.h>

#include "debug.h"
#include "mutex.h"
#include "panic.h"
#include "all_defs.h"


extern char __heap_start[];
extern char __heap_end[];
extern char __heap_start_phys[];

Result(HeapCode) Heap__init(HeapPtr self)
{
    *self = (Heap) {
        .start = addr_of(__heap_start),
        .end = addr_of(__heap_end),
        .phys_start = addr_of(__heap_start_phys),
        .current_block = (MemoryBlock) {
            .start = KERNEL_ADDR(addr_of(__heap_start), addr_of(__heap_start_phys)),
            .pages_allocated = 0,
            .block_lock = MUTEX_INIT,
        }
    };
    return Success(HeapCode, 0);
}

inline MemoryPagePtr align_up(MemoryPagePtr page, Address a){
    Address x = addr_of(page);
    return (MemoryPagePtr)((x + (a - 1)) & ~(a - 1));
}

static void MemoryBlock__advance(MemoryBlockPtr self, Address heap_end) {
    KernelAddress start = self->start;

    Address next_phys_addr = start.phys_addr + COARSE_BLOCK_SIZE;
    Address next_virtual_addr = start.virtual_addr + COARSE_BLOCK_SIZE;

    __d_printf(DEBUG, "advancing block to %p\n", next_virtual_addr);

    if (next_virtual_addr > heap_end)
    {
        Kernel__panic("out of heap", NULL);
    }

    *self = (MemoryBlock) {
        .pages_allocated = 0,
        .start = KERNEL_ADDR(next_virtual_addr, next_phys_addr),
    };
}

static KernelAddress MemoryBlock__next_page(MemoryBlockPtr self, Address heap_end)
{
    mutex_lock(&self->block_lock);

    uint64_t page_index = self->pages_allocated;

    if (page_index >= PAGES_PER_BLOCK)
    {
        MemoryBlock__advance(self, heap_end);
    }
    self->pages_allocated++;
    KernelAddress start = self->start;

    mutex_unlock(&self->block_lock);

    Address virtual_addr = start.virtual_addr + (page_index * PAGE_SIZE);
    Address physical_addr = start.phys_addr + (page_index * PAGE_SIZE);

    return KERNEL_ADDR(virtual_addr, physical_addr);
}

static KernelAddress MemoryBlock__next_pages(MemoryBlockPtr self, Address heap_end, size_t num_pages)
{
    mutex_lock(&self->block_lock);

    KernelAddress first = INVALID_KERNEL_ADDR;

    // assumption is the blocks and pages are sequential
    for (uint64_t i = 0; i < num_pages; i++)
    {
        if (self->pages_allocated >= PAGES_PER_BLOCK)
        {
            MemoryBlock__advance(self, heap_end);
        }
        KernelAddress start = self->start;
        uint64_t pages_allocated = self->pages_allocated;

        Address virtual_addr = start.virtual_addr + (pages_allocated * PAGE_SIZE);
        Address physical_addr = start.phys_addr + (pages_allocated * PAGE_SIZE);

        self->pages_allocated++;
        if (!first.valid)
        {
            first = KERNEL_ADDR(virtual_addr, physical_addr);
        }
    }
    mutex_unlock(&self->block_lock);
    return first;
}

static KernelAddress Heap__next(HeapPtr self)
{
    return MemoryBlock__next_page(&self->current_block, self->end);
}

static KernelAddress Heap__next_pages(HeapPtr self, size_t num_pages)
{
    return MemoryBlock__next_pages(&self->current_block, self->end, num_pages);
}

KernelAddress Heap__alloc_fresh(HeapPtr self) {
    KernelAddress page = Heap__next(self);

    MemoryPage__zero(v_ptr(page, MemoryPage));;
    return page;
}

KernelAddress Heap__alloc_fresh_size(HeapPtr self, size_t size) {
    uint64_t pages = size / PAGE_SIZE;
    pages += (size % PAGE_SIZE) ? 1 : 0;

    KernelAddress page = Heap__next_pages(self, pages);
    MemoryPage__zero((MemoryPage *)page.virtual_addr);
    return page;
}

KernelAddress Heap__alloc(HeapPtr self)
{
    return Heap__alloc_fresh(self);
}

KernelAddress Heap__alloc_size(HeapPtr self, size_t size)
{
    return Heap__alloc_fresh_size(self, size);
}

KernelAddress Heap__from_virtual_address(HeapPtr self, Address virtual)
{
    Address offset = virtual - self->start;
    Address physical = offset + self->phys_start;

    if (virtual < self->start) {
        // is it one of our allocated addresses?
        __d_printf(PANIC, "virtual=%p is not from managed heap %p (phys=%p)\n",
            virtual, self->start, self->phys_start);
        return INVALID_KERNEL_ADDR;
    }

    return KERNEL_ADDR(virtual, physical);
}

KernelAddress Heap__from_physical_address(HeapPtr self, Address physical)
{
    Address offset = physical - self->phys_start;
    Address virtual = offset + self->start;

    if (physical < self->phys_start) {
        __d_printf(PANIC, "Unknown physical addr=%p", physical);
        // is it one of our allocated addresses?
        return INVALID_KERNEL_ADDR;
    }

    return KERNEL_ADDR(virtual, physical);
}

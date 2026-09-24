#include "heap.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "memory_page.h"

#include "debug.h"
#include "mutex.h"
#include "panic.h"
#include "all_defs.h"

HashtableBodyDefine(AllocatedBlocks);

HashCode Heap__hash_address(AddressPtr self)
{
    return *self / PAGE_SIZE;
}

static MemoryBlock MemoryBlock__new(KernelAddress start, uint64_t pages_allocated)
{
    return (MemoryBlock){
        .start = start,
        .block_lock = MUTEX_INIT,
        .pages_allocated = pages_allocated,
    };
}

static size_t Heap__coarse_block_size(SystemInfoPtr system_info)
{
    if (system_info->heap_coarse_block_size == 0)
    {
        return SystemInfo__default_heap_coarse_block_size();
    }

    return system_info->heap_coarse_block_size;
}

static Address Heap__align_up_page(Address value)
{
    Address remainder = value % PAGE_SIZE;
    if (remainder == 0)
    {
        return value;
    }

    Address adjustment = PAGE_SIZE - remainder;
    if (value > UINT64_MAX - adjustment)
    {
        return UINT64_MAX;
    }

    return value + adjustment;
}

static Optional(MemoryBlock) Heap__block_for_region(HeapPtr self, ConstPhysicalMemoryRegionPtr region)
{
    Address heap_physical_end = self->phys_start + (self->end - self->start);
    Address physical_start = region->physical_start > self->phys_start ? region->physical_start : self->phys_start;
    Address physical_end = region->physical_end < heap_physical_end ? region->physical_end : heap_physical_end;

    physical_start = Heap__align_up_page(physical_start);
    physical_end -= physical_end % PAGE_SIZE;
    if (physical_end <= physical_start)
    {
        return None(MemoryBlock);
    }

    Address virtual_start = self->start + (physical_start - self->phys_start);
    uint64_t pages = (physical_end - physical_start) / PAGE_SIZE;
    return Some(MemoryBlock, MemoryBlock__new(KERNEL_ADDR(virtual_start, physical_start), pages));
}

static MemoryBlock Heap__coalesce_free_block(HeapPtr self, MemoryBlock block);

static Result(HeapCode) Heap__add_free_region(HeapPtr self, ConstPhysicalMemoryRegionPtr region)
{
    match_optional(
        Heap__block_for_region(self, region),
        some(block)
        {
            MemoryBlock free_block = Heap__coalesce_free_block(self, block);
            match_result(
                AllocatedBlocks__set(&self->free_blocks, free_block.start.virtual_addr, free_block),
                success(_)
                {
                    return Success(HeapCode, HEAP_OK);
                },
                error(code, message)
                {
                    return Error(HeapCode, message, HEAP_OUT_OF_MEMORY);
                }
            );
        },
        none()
        {
            return Success(HeapCode, HEAP_OK);
        }
    );

    return Error(HeapCode, bstring("failed to add heap region"), HEAP_OUT_OF_MEMORY);
}

Result(HeapCode) Heap__init(HeapPtr self, SystemInfoPtr system_info)
{
    MemoryRegionPtr kernel_heap = &system_info->kernel_heap;
    self->lock = MUTEX_INIT;
    self->start = kernel_heap->virtual_start;
    self->end = kernel_heap->virtual_start + (kernel_heap->physical_end - kernel_heap->physical_start);
    self->phys_start = kernel_heap->physical_start;
    self->coarse_block_size = Heap__coarse_block_size(system_info);
    AllocatedBlocks__init(&self->allocated_blocks);
    AllocatedBlocks__init(&self->free_blocks);

    if (PhysicalMemoryRegions__len(&system_info->physical_memory.free_regions) == 0)
    {
        PhysicalMemoryRegion fallback =
            PhysicalMemoryRegion__new(system_info->kernel_heap.physical_start, system_info->kernel_heap.physical_end);
        return Heap__add_free_region(self, &fallback);
    }

    for_each_fa_ptr(
        PhysicalMemoryRegions,
        &system_info->physical_memory.free_regions,
        index,
        region,
        {
            match_result(
                Heap__add_free_region(self, &region),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(HeapCode, message, code);
                }
            );
        }
    );

    return Success(HeapCode, HEAP_OK);
}

static uint64_t Heap__pages_for_size(size_t size)
{
    uint64_t pages = size / PAGE_SIZE;
    pages += (size % PAGE_SIZE) ? 1 : 0;
    return pages > 0 ? pages : 1;
}

static void Heap__zero_pages(KernelAddress start, uint64_t pages)
{
    for (uint64_t i = 0; i < pages; i++)
    {
        MemoryPagePtr page = (MemoryPagePtr)(start.virtual_addr + (i * PAGE_SIZE));
        MemoryPage__zero(page);
    }
}

static void AllocatedBlocks__remove_entry(AllocatedBlocksPtr blocks, uint64_t table_index, uint64_t bucket_index)
{
    AllocatedBlocksBucketPtr bucket = &blocks->table.entries[table_index].value;

    for (uint64_t i = bucket_index; i + 1 < bucket->len; i++)
    {
        bucket->data[i] = bucket->data[i + 1];
    }
    bucket->len--;
    blocks->len--;

    if (bucket->len == 0)
    {
        AllocatedBlocks__table__delete(&blocks->table, blocks->table.entries[table_index].key);
    }
}

static bool AllocatedBlocks__remove_key(AllocatedBlocksPtr blocks, Address key)
{
    if (is_nothing(AllocatedBlocks__get(blocks, key)))
    {
        return false;
    }

    AllocatedBlocks__delete(blocks, key);
    return true;
}

static Optional(MemoryBlock) Heap__take_free_block(HeapPtr self, uint64_t pages)
{
    for (uint64_t table_index = 0; table_index < (uint64_t)self->free_blocks.table.len; table_index++)
    {
        AllocatedBlocksBucketPtr bucket = &self->free_blocks.table.entries[table_index].value;

        for (uint64_t bucket_index = 0; bucket_index < bucket->len; bucket_index++)
        {
            MemoryBlock block = bucket->data[bucket_index].value;

            if (block.pages_allocated >= pages)
            {
                AllocatedBlocks__remove_entry(&self->free_blocks, table_index, bucket_index);
                if (block.pages_allocated > pages)
                {
                    KernelAddress remainder_start = KERNEL_ADDR(block.start.virtual_addr + (pages * PAGE_SIZE),
                                                                block.start.phys_addr + (pages * PAGE_SIZE));
                    MemoryBlock remainder = MemoryBlock__new(remainder_start, block.pages_allocated - pages);
                    AllocatedBlocks__set(&self->free_blocks, remainder.start.virtual_addr, remainder);
                    block.pages_allocated = pages;
                }
                return Some(MemoryBlock, block);
            }
        }
    }

    return None(MemoryBlock);
}

static Result(HeapCode) Heap__record_allocated(HeapPtr self, KernelAddress start, uint64_t pages)
{
    MemoryBlock block = MemoryBlock__new(start, pages);

    match_result(
        AllocatedBlocks__set(&self->allocated_blocks, start.virtual_addr, block),
        success(_)
        {
            return Success(HeapCode, HEAP_OK);
        },
        error(code, message)
        {
            __d_printf(SYSLOG, "heap allocation tracking failed addr=%p pages=%i code=%i message=%s\n",
                       start.virtual_addr, pages, code, BString__cstr(&message));
            return Error(HeapCode, message, code);
        }
    );

    return Error(HeapCode, bstring("allocation tracking failed"), HEAP_NOT_IMPLEMENTED);
}

static bool Heap__coalesce_one_free_block(HeapPtr self, MemoryBlockPtr block)
{
    Address block_start = block->start.virtual_addr;
    Address block_end = block_start + (block->pages_allocated * PAGE_SIZE);

    for (uint64_t table_index = 0; table_index < (uint64_t)self->free_blocks.table.len; table_index++)
    {
        AllocatedBlocksBucketPtr bucket = &self->free_blocks.table.entries[table_index].value;

        for (uint64_t bucket_index = 0; bucket_index < bucket->len; bucket_index++)
        {
            MemoryBlock other = bucket->data[bucket_index].value;
            Address other_start = other.start.virtual_addr;
            Address other_end = other_start + (other.pages_allocated * PAGE_SIZE);

            if (other_end == block_start)
            {
                block->start = other.start;
                block->pages_allocated += other.pages_allocated;
                AllocatedBlocks__remove_entry(&self->free_blocks, table_index, bucket_index);
                return true;
            }

            if (block_end == other_start)
            {
                block->pages_allocated += other.pages_allocated;
                AllocatedBlocks__remove_entry(&self->free_blocks, table_index, bucket_index);
                return true;
            }
        }
    }

    return false;
}

static MemoryBlock Heap__coalesce_free_block(HeapPtr self, MemoryBlock block)
{
    while (Heap__coalesce_one_free_block(self, &block))
    {
    }

    return block;
}

KernelAddress Heap__alloc(HeapPtr self)
{
    return Heap__alloc_size(self, PAGE_SIZE);
}

static KernelAddress Heap__allocate_available(HeapPtr self, uint64_t pages, size_t size)
{
    match_optional(
        Heap__take_free_block(self, pages),
        some(block)
        {
            Heap__zero_pages(block.start, block.pages_allocated);
            match_result(
                Heap__record_allocated(self, block.start, block.pages_allocated),
                success(_)
                {
                    __d_printf(MEMORY, "heap alloc addr=%p phys=%p pages=%i size=%i\n", block.start.virtual_addr,
                               block.start.phys_addr, block.pages_allocated, size);
                    return block.start;
                },
                error(code, message)
                {
                    __d_printf(SYSLOG, "heap allocation untracked addr=%p message=%s\n", block.start.virtual_addr,
                               BString__cstr(&message));
                    return INVALID_KERNEL_ADDR;
                }
            );
        },
        none()
        {
            return INVALID_KERNEL_ADDR;
        }
    );

    return INVALID_KERNEL_ADDR;
}

KernelAddress Heap__alloc_size(HeapPtr self, size_t size)
{
    critical_section(
        self->lock,
        {
            uint64_t pages = Heap__pages_for_size(size);
            KernelAddress allocation = Heap__allocate_available(self, pages, size);
            if (!allocation.valid)
            {
Kernel__panic("out of heap", NULL); // hundelint: ignore-no-null
            }
            cs_return allocation;
        }
    );

    return INVALID_KERNEL_ADDR;
}

KernelAddress Heap__try_alloc_size(HeapPtr self, size_t size)
{
    critical_section(
        self->lock,
        {
            uint64_t pages = Heap__pages_for_size(size);
            KernelAddress allocation = Heap__allocate_available(self, pages, size);
            if (!allocation.valid)
            {
                __d_printf(MEMORY, "heap alloc failed size=%i pages=%i\n", size, pages);
            }
            cs_return allocation;
        }
    );

    return INVALID_KERNEL_ADDR;
}

Result(HeapCode) Heap__free(HeapPtr self, KernelAddress addr)
{
    critical_section(
        self->lock,
        {
            HashtableOptional(AllocatedBlocks) block = AllocatedBlocks__get(&self->allocated_blocks, addr.virtual_addr);

            match_optional(
                block,
                some(allocated)
                {
                    AllocatedBlocks__remove_key(&self->allocated_blocks, addr.virtual_addr);
                    MemoryBlock free_block = Heap__coalesce_free_block(self, allocated);
                    AllocatedBlocks__set(&self->free_blocks, free_block.start.virtual_addr, free_block);
                    __d_printf(MEMORY, "heap free addr=%p phys=%p pages=%i coalesced_addr=%p coalesced_pages=%i\n",
                               allocated.start.virtual_addr, allocated.start.phys_addr, allocated.pages_allocated,
                               free_block.start.virtual_addr, free_block.pages_allocated);
                    cs_return Success(HeapCode, HEAP_OK);
                },
                none_do_nothing()
            );

            cs_return Error(HeapCode, bstring("address not allocated"), HEAP_NOT_IMPLEMENTED);
        }
    );

    return Error(HeapCode, bstring("address not allocated"), HEAP_NOT_IMPLEMENTED);
}

KernelAddress Heap__from_virtual_address(HeapPtr self, Address virtual)
{
    Address offset = virtual - self->start;
    Address physical = offset + self->phys_start;

    if (virtual < self->start)
    {
        // is it one of our allocated addresses?
        __d_printf(PANIC, "virtual=%p is not from managed heap %p (phys=%p)\n", virtual, self->start, self->phys_start);
        return INVALID_KERNEL_ADDR;
    }

    return KERNEL_ADDR(virtual, physical);
}

KernelAddress Heap__try_from_virtual_address(HeapPtr self, Address virtual)
{
    if (virtual < self->start || virtual >= self->end)
    {
        return INVALID_KERNEL_ADDR;
    }

    Address offset = virtual - self->start;
    return KERNEL_ADDR(virtual, offset + self->phys_start);
}

KernelAddress Heap__from_physical_address(HeapPtr self, Address physical)
{
    Address offset = physical - self->phys_start;
    Address virtual = offset + self->start;

    if (physical < self->phys_start)
    {
        __d_printf(PANIC, "Unknown physical addr=%p", physical);
        // is it one of our allocated addresses?
        return INVALID_KERNEL_ADDR;
    }

    return KERNEL_ADDR(virtual, physical);
}

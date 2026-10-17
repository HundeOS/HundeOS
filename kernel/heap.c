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
            self->total_pages += block.pages_allocated;
            self->free_pages += block.pages_allocated;
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
    self->total_pages = 0;
    self->free_pages = 0;
    self->pressure_generation = 1;
    self->pressure_level = HEAP_PRESSURE_NORMAL;

    if (PhysicalMemoryRegions__len(&system_info->physical_memory.free_regions) == 0)
    {
        PhysicalMemoryRegion fallback =
            PhysicalMemoryRegion__new(system_info->kernel_heap.physical_start, system_info->kernel_heap.physical_end);
        match_result(
            Heap__add_free_region(self, &fallback),
            success(_)
            {
                return Success(HeapCode, HEAP_OK);
            },
            error(code, message)
            {
                return Error(HeapCode, message, code);
            }
        );
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

static HeapMemoryState HeapMemoryState__new(uint64_t total_pages, uint64_t free_pages, uint64_t pressure_generation,
                                            HeapPressureLevel pressure_level)
{
    return (HeapMemoryState){
        .total_pages = total_pages,
        .free_pages = free_pages,
        .pressure_generation = pressure_generation,
        .pressure_level = pressure_level,
    };
}

static HeapPressureLevel Heap__calculate_pressure(HeapPtr self)
{
    if (self->total_pages == 0)
    {
        return HEAP_PRESSURE_CRITICAL;
    }

    if (self->free_pages <= self->total_pages / 10U)
    {
        return HEAP_PRESSURE_CRITICAL;
    }
    if (self->free_pages <= self->total_pages / 5U)
    {
        return HEAP_PRESSURE_LOW;
    }
    return HEAP_PRESSURE_NORMAL;
}

static void Heap__update_pressure(HeapPtr self)
{
    HeapPressureLevel pressure = Heap__calculate_pressure(self);
    if (pressure != self->pressure_level)
    {
        self->pressure_level = pressure;
        self->pressure_generation++;
    }
}

static void Heap__zero_pages(KernelAddress start, uint64_t pages)
{
    for (uint64_t i = 0; i < pages; i++)
    {
        MemoryPagePtr page = (MemoryPagePtr)(start.virtual_addr + (i * PAGE_SIZE));
        MemoryPage__zero(page);
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
    Optional(MemoryBlock) best = None(MemoryBlock);
    for_each_hashtable_ptr(
        AllocatedBlocks,
        &self->free_blocks,
        table_index,
        bucket_index,
        key,
        candidate,
        {
            if (candidate->pages_allocated >= pages)
            {
                match_optional(
                    best,
                    some(current)
                    {
                        if (candidate->pages_allocated < current.pages_allocated)
                        {
                            best = Some(MemoryBlock, *candidate);
                        }
                    },
                    none()
                    {
                        best = Some(MemoryBlock, *candidate);
                    }
                );
            }
        }
    );

    match_optional(
        best,
        some(block)
        {
            AllocatedBlocks__delete(&self->free_blocks, block.start.virtual_addr);
            if (block.pages_allocated > pages)
            {
                KernelAddress remainder_start = KERNEL_ADDR(block.start.virtual_addr + (pages * PAGE_SIZE),
                                                            block.start.phys_addr + (pages * PAGE_SIZE));
                MemoryBlock remainder = MemoryBlock__new(remainder_start, block.pages_allocated - pages);
                AllocatedBlocks__set(&self->free_blocks, remainder.start.virtual_addr, remainder);
                block.pages_allocated = pages;
            }
            return Some(MemoryBlock, block);
        },
        none()
        {
            return None(MemoryBlock);
        }
    );

    return None(MemoryBlock);
}

static Optional(MemoryBlock) Heap__take_aligned_free_block(HeapPtr self, uint64_t pages, size_t alignment)
{
    for_each_hashtable_ptr(
        AllocatedBlocks,
        &self->free_blocks,
        table_index,
        bucket_index,
        key,
        candidate,
        {
            MemoryBlock block = *candidate;
            Address remainder = block.start.phys_addr % alignment;
            Address adjustment = remainder == 0 ? 0 : alignment - remainder;
            uint64_t prefix_pages = adjustment / PAGE_SIZE;
            if (prefix_pages + pages > block.pages_allocated)
            {
                continue;
            }

            AllocatedBlocks__delete(&self->free_blocks, *key);
            if (prefix_pages > 0)
            {
                MemoryBlock prefix = MemoryBlock__new(block.start, prefix_pages);
                AllocatedBlocks__set(&self->free_blocks, prefix.start.virtual_addr, prefix);
            }

            KernelAddress aligned_start = KERNEL_ADDR(block.start.virtual_addr + adjustment,
                                                      block.start.phys_addr + adjustment);
            uint64_t suffix_pages = block.pages_allocated - prefix_pages - pages;
            if (suffix_pages > 0)
            {
                KernelAddress suffix_start =
                    KERNEL_ADDR(aligned_start.virtual_addr + (pages * PAGE_SIZE),
                                aligned_start.phys_addr + (pages * PAGE_SIZE));
                MemoryBlock suffix = MemoryBlock__new(suffix_start, suffix_pages);
                AllocatedBlocks__set(&self->free_blocks, suffix.start.virtual_addr, suffix);
            }
            return Some(MemoryBlock, MemoryBlock__new(aligned_start, pages));
        }
    );

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

    for_each_hashtable_ptr(
        AllocatedBlocks,
        &self->free_blocks,
        table_index,
        bucket_index,
        key,
        candidate,
        {
            MemoryBlock other = *candidate;
            Address other_start = other.start.virtual_addr;
            Address other_end = other_start + (other.pages_allocated * PAGE_SIZE);

            if (other_end == block_start)
            {
                block->start = other.start;
                block->pages_allocated += other.pages_allocated;
                AllocatedBlocks__delete(&self->free_blocks, *key);
                return true;
            }

            if (block_end == other_start)
            {
                block->pages_allocated += other.pages_allocated;
                AllocatedBlocks__delete(&self->free_blocks, *key);
                return true;
            }
        }
    );

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
                    self->free_pages -= block.pages_allocated;
                    Heap__update_pressure(self);
                    __d_printf(MEMORY, "heap alloc addr=%p phys=%p pages=%i size=%i\n", block.start.virtual_addr,
                               block.start.phys_addr, block.pages_allocated, size);
                    return block.start;
                },
                error(code, message)
                {
                    MemoryBlock free_block = Heap__coalesce_free_block(self, block);
                    AllocatedBlocks__set(&self->free_blocks, free_block.start.virtual_addr, free_block);
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

KernelAddress Heap__try_alloc_reclaimable_size(HeapPtr self, size_t size)
{
    critical_section(
        self->lock,
        {
            uint64_t pages = Heap__pages_for_size(size);
            uint64_t growth_floor = self->total_pages / 2U;
            if (pages > self->free_pages || self->free_pages - pages < growth_floor)
            {
                cs_return INVALID_KERNEL_ADDR;
            }
            cs_return Heap__allocate_available(self, pages, size);
        }
    );

    return INVALID_KERNEL_ADDR;
}

HeapMemoryState Heap__memory_state(HeapPtr self)
{
    critical_section(
        self->lock,
        {
            HeapMemoryState state =
                HeapMemoryState__new(self->total_pages, self->free_pages, self->pressure_generation,
                                     (HeapPressureLevel)self->pressure_level);
            cs_return state;
        }
    );

    return HeapMemoryState__new(0, 0, 0, HEAP_PRESSURE_CRITICAL);
}

KernelAddress Heap__try_alloc_size_aligned(HeapPtr self, size_t size, size_t alignment)
{
    if (alignment < PAGE_SIZE || alignment % PAGE_SIZE != 0)
    {
        return INVALID_KERNEL_ADDR;
    }

    critical_section(
        self->lock,
        {
            uint64_t pages = Heap__pages_for_size(size);
            match_optional(
                Heap__take_aligned_free_block(self, pages, alignment),
                some(block)
                {
                    Heap__zero_pages(block.start, block.pages_allocated);
                    match_result(
                        Heap__record_allocated(self, block.start, block.pages_allocated),
                        success(_)
                        {
                            self->free_pages -= block.pages_allocated;
                            Heap__update_pressure(self);
                            cs_return block.start;
                        },
                        error(code, message)
                        {
                            MemoryBlock free_block = Heap__coalesce_free_block(self, block);
                            AllocatedBlocks__set(&self->free_blocks, free_block.start.virtual_addr, free_block);
                            __d_printf(MEMORY, "aligned heap allocation tracking failed code=%i message=%s\n", code,
                                       BString__cstr(&message));
                        }
                    );
                },
                none_do_nothing()
            );
            cs_return INVALID_KERNEL_ADDR;
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
                    self->free_pages += allocated.pages_allocated;
                    Heap__update_pressure(self);
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

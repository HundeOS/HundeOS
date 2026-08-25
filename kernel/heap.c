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

HashCode Heap__hash_address(Address * self)
{
    return *self / PAGE_SIZE;
}

Result(HeapCode) Heap__init(HeapPtr self, SystemInfoPtr system_info)
{
    MemoryRegionPtr kernel_heap = &system_info->kernel_heap;

    *self = (Heap) {
        .lock = MUTEX_INIT,
        .start = kernel_heap->virtual_start,
        .end = kernel_heap->virtual_start + (kernel_heap->physical_end - kernel_heap->physical_start),
        .phys_start = kernel_heap->physical_start,
        .current_block = (MemoryBlock) {
            .start = KERNEL_ADDR(kernel_heap->virtual_start, kernel_heap->physical_start),
            .pages_allocated = 0,
            .block_lock = MUTEX_INIT,
        },
        .allocated_blocks = AllocatedBlocks__new(),
        .free_blocks = AllocatedBlocks__new(),
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
        Kernel__panic("out of heap", NULL); // hundelint: ignore-no-null
    }

    *self = (MemoryBlock) {
        .pages_allocated = 0,
        .start = KERNEL_ADDR(next_virtual_addr, next_phys_addr),
        .block_lock = MUTEX_INIT,
    };
}

static KernelAddress MemoryBlock__next_page(MemoryBlockPtr self, Address heap_end)
{
    mutex_lock(&self->block_lock);

    uint64_t page_index = self->pages_allocated;

    if (page_index >= PAGES_PER_BLOCK)
    {
        MemoryBlock__advance(self, heap_end);
        page_index = self->pages_allocated;
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

static void AllocatedBlocks__remove_entry(AllocatedBlocks * blocks, uint64_t table_index, uint64_t bucket_index)
{
    AllocatedBlocks__bucket * bucket = &blocks->table.entries[table_index].value;

    for (uint64_t i = bucket_index; i + 1 < bucket->len; i++)
    {
        bucket->data[i] = bucket->data[i + 1];
    }
    bucket->len--;

    if (bucket->len == 0)
    {
        AllocatedBlocks__table__delete(&blocks->table, blocks->table.entries[table_index].key);
    }
}

static bool AllocatedBlocks__remove_key(AllocatedBlocks * blocks, Address key)
{
    HashCode hash = Heap__hash_address(&key) % AllocatedBlocks__table_size;

    for (uint64_t table_index = 0; table_index < (uint64_t)blocks->table.len; table_index++)
    {
        if (blocks->table.entries[table_index].key != hash)
        {
            continue;
        }

        AllocatedBlocks__bucket * bucket = &blocks->table.entries[table_index].value;

        for (uint64_t bucket_index = 0; bucket_index < bucket->len; bucket_index++)
        {
            if (bucket->data[bucket_index].key == key)
            {
                AllocatedBlocks__remove_entry(blocks, table_index, bucket_index);
                return true;
            }
        }
    }

    return false;
}

static Optional(MemoryBlock) Heap__take_free_block(HeapPtr self, uint64_t pages)
{
    for (uint64_t table_index = 0; table_index < (uint64_t)self->free_blocks.table.len; table_index++)
    {
        AllocatedBlocks__bucket * bucket = &self->free_blocks.table.entries[table_index].value;

        for (uint64_t bucket_index = 0; bucket_index < bucket->len; bucket_index++)
        {
            MemoryBlock block = bucket->data[bucket_index].value;

            if (block.pages_allocated >= pages)
            {
                AllocatedBlocks__remove_entry(&self->free_blocks, table_index, bucket_index);
                if (block.pages_allocated > pages)
                {
                    KernelAddress remainder_start = KERNEL_ADDR(
                        block.start.virtual_addr + (pages * PAGE_SIZE),
                        block.start.phys_addr + (pages * PAGE_SIZE)
                    );
                    MemoryBlock remainder = (MemoryBlock) {
                        .start = remainder_start,
                        .block_lock = MUTEX_INIT,
                        .pages_allocated = block.pages_allocated - pages,
                    };
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
    MemoryBlock block = (MemoryBlock) {
        .start = start,
        .block_lock = MUTEX_INIT,
        .pages_allocated = pages,
    };

    match_result(CollectionCode, AllocatedBlocks__set(&self->allocated_blocks, start.virtual_addr, block),
        success(_) { return Success(HeapCode, HEAP_OK); },
        error(code, message)
        {
            __d_printf(
                SYSLOG,
                "heap allocation tracking failed addr=%p pages=%i code=%i message=%s\n",
                start.virtual_addr,
                pages,
                code,
                BString__cstr(&message)
            );
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
        AllocatedBlocks__bucket * bucket = &self->free_blocks.table.entries[table_index].value;

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

KernelAddress Heap__alloc_fresh(HeapPtr self) {
    KernelAddress page = Heap__next(self);

    MemoryPage__zero(v_ptr(page, MemoryPage));
    match_result(HeapCode, Heap__record_allocated(self, page, 1),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(SYSLOG, "heap fresh allocation untracked addr=%p message=%s\n", page.virtual_addr, BString__cstr(&message));
            return INVALID_KERNEL_ADDR;
        }
    );
    return page;
}

KernelAddress Heap__alloc_fresh_size(HeapPtr self, size_t size) {
    uint64_t pages = Heap__pages_for_size(size);

    KernelAddress page = Heap__next_pages(self, pages);
    Heap__zero_pages(page, pages);
    match_result(HeapCode, Heap__record_allocated(self, page, pages),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(SYSLOG, "heap fresh allocation untracked addr=%p message=%s\n", page.virtual_addr, BString__cstr(&message));
            return INVALID_KERNEL_ADDR;
        }
    );
    return page;
}

KernelAddress Heap__alloc(HeapPtr self)
{
    return Heap__alloc_size(self, PAGE_SIZE);
}

KernelAddress Heap__alloc_size(HeapPtr self, size_t size)
{
    critical_section(self->lock,
    {
        uint64_t pages = Heap__pages_for_size(size);

        match_optional(MemoryBlock, Heap__take_free_block(self, pages),
            some(block)
            {
                Heap__zero_pages(block.start, block.pages_allocated);
                match_result(HeapCode, Heap__record_allocated(self, block.start, block.pages_allocated),
                    success(_) { cs_return block.start; },
                    error(code, message)
                    {
                        __d_printf(
                            SYSLOG,
                            "heap reused allocation untracked addr=%p message=%s\n",
                            block.start.virtual_addr,
                            BString__cstr(&message)
                        );
                        cs_return INVALID_KERNEL_ADDR;
                    }
                );
            },
            none_do_nothing()
        );

        cs_return Heap__alloc_fresh_size(self, size);
    });

    return INVALID_KERNEL_ADDR;
}

Result(HeapCode) Heap__free(HeapPtr self, KernelAddress addr)
{
    critical_section(self->lock,
    {
        HashtableOptional(AllocatedBlocks) block = AllocatedBlocks__get(&self->allocated_blocks, addr.virtual_addr);

        match_optional(MemoryBlock, block,
            some(allocated)
            {
                AllocatedBlocks__remove_key(&self->allocated_blocks, addr.virtual_addr);
                MemoryBlock free_block = Heap__coalesce_free_block(self, allocated);
                AllocatedBlocks__set(&self->free_blocks, free_block.start.virtual_addr, free_block);
                cs_return Success(HeapCode, HEAP_OK);
            },
            none_do_nothing()
        );

        cs_return Error(HeapCode, bstring("address not allocated"), HEAP_NOT_IMPLEMENTED);
    });

    return Error(HeapCode, bstring("address not allocated"), HEAP_NOT_IMPLEMENTED);
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

    if (physical < self->phys_start) {
        __d_printf(PANIC, "Unknown physical addr=%p", physical);
        // is it one of our allocated addresses?
        return INVALID_KERNEL_ADDR;
    }

    return KERNEL_ADDR(virtual, physical);
}

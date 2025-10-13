#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "page_alloc.h"

#include "debug.h"
#include "mutex.h"
#include "panic.h"
#include "all_defs.h"

#define COARSE_BLOCK_SIZE _4MB
#define PAGES_PER_BLOCK (COARSE_BLOCK_SIZE / PAGE_SIZE)

extern char __heap_start[];
extern char __heap_end[];
extern char __heap_start_phys[];

// later we can avoid most locks by having different blocks per cpu
static mutex_t block_lock = MUTEX_INIT;

typedef struct MemoryBlock {
    KernelAddress start;
    uint64_t pages_allocated;
    bool bootstrapped;
} MemoryBlock;

MemoryPage * MemoryPage__zero(MemoryPage * self){
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        self->page[i] = 0;
    }
    return self;
}


static MemoryBlock current_block = {
    .start = {
        .phys_addr = 0,
        .virtual_addr = 0
    },
    .pages_allocated = 0,
    .bootstrapped = false
};

inline MemoryPage * align_up(MemoryPage * page, Address a){
    Address x = (Address)page;
    return (MemoryPage *)((x + (a - 1)) & ~(a - 1));
}

static void MemoryBlock__advance() {
    Address next_phys_addr = current_block.start.phys_addr + COARSE_BLOCK_SIZE;
    Address next_virtual_addr = current_block.start.virtual_addr + COARSE_BLOCK_SIZE;
    __d_printf(DEBUG, "advancing block to %p\n", next_virtual_addr);
    if (next_virtual_addr > addr_of(__heap_end)) {
        Kernel__panic("out of heap", NULL);
    }

    current_block.pages_allocated = 0;
    current_block.start.phys_addr = next_phys_addr;
    current_block.start.virtual_addr = next_virtual_addr;
}

static void MemoryBlock__bootstrap(){
    // we need to allocate the first block
    current_block.start.phys_addr = addr_of(__heap_start_phys); // work out if there is an offset
    current_block.start.virtual_addr = addr_of(__heap_start);
    current_block.pages_allocated = 0;
    current_block.bootstrapped = true;

    __d_printf(DEBUG, "start_phys=%p, start_virt=%p\n",current_block.start.phys_addr, current_block.start.virtual_addr);
}


static KernelAddress MemoryBlock__next_page() {
    mutex_lock(&block_lock);
    if (!current_block.bootstrapped) {
        MemoryBlock__bootstrap();
    }

    uint64_t page_index = current_block.pages_allocated;

    if (page_index >= PAGES_PER_BLOCK) {
        MemoryBlock__advance();
    }
    current_block.pages_allocated++;
    mutex_unlock(&block_lock);
    Address virtual_addr = current_block.start.virtual_addr + (page_index * PAGE_SIZE);
    Address physical_addr = current_block.start.phys_addr + (page_index * PAGE_SIZE);

    return (KernelAddress) {
        .virtual_addr = virtual_addr,
        .phys_addr = physical_addr,
        .valid = true
    };
}

static KernelAddress MemoryBlock__next_pages(size_t num_pages) {
    mutex_lock(&block_lock);
    if (!current_block.bootstrapped) {
        MemoryBlock__bootstrap();
    }
    KernelAddress first = (KernelAddress) {
        .virtual_addr = 0,
        .phys_addr = 0,
        .valid = false
    };
    // assumption is the blocks and pages are sequential
    for (uint64_t i = 0; i < num_pages; i++)
    {
        if (current_block.pages_allocated >= PAGES_PER_BLOCK)
        {
            MemoryBlock__advance();
        }
        Address virtual_addr = current_block.start.virtual_addr + (current_block.pages_allocated * PAGE_SIZE);
        Address physical_addr = current_block.start.phys_addr + (current_block.pages_allocated * PAGE_SIZE);

        current_block.pages_allocated++;
        if (!first.valid)
        {
            first = (KernelAddress) {
                .virtual_addr = virtual_addr,
                .phys_addr = physical_addr,
                .valid = true
            };
        }
    }
    mutex_unlock(&block_lock);
    return first;
}

static KernelAddress MemoryPage__next()
{
    return MemoryBlock__next_page();
}

static KernelAddress MemoryPage__next_pages(size_t num_pages)
{
    return MemoryBlock__next_pages(num_pages);
}

KernelAddress MemoryPage__alloc_fresh() {
    KernelAddress page = MemoryPage__next();

    MemoryPage__zero(v_ptr(page, MemoryPage));;
    return page;
}

KernelAddress MemoryPage__alloc_fresh_size(size_t size) {
    uint64_t pages = size / PAGE_SIZE;
    pages += (size % PAGE_SIZE) ? 1 : 0;

    KernelAddress page = MemoryPage__next_pages(pages);
    MemoryPage__zero((MemoryPage *)page.virtual_addr);
    return page;
}

KernelAddress MemoryPage__alloc(void)
{

    return MemoryPage__alloc_fresh();
}

KernelAddress MemoryPage__alloc_size(size_t size)
{
    return MemoryPage__alloc_fresh_size(size);
}

KernelAddress MemoryPage__from_virtual_address(Address virtual)
{
    Address offset = virtual - addr_of(__heap_start);
    Address physical = offset + addr_of(__heap_start_phys);

    if (virtual < addr_of(__heap_start)) {
        // is it one of our allocated addresses?
        __d_printf(PANIC, "virtual=%p is not from managed heap %p (phys=%p)\n",
            virtual, addr_of(__heap_start), addr_of(__heap_start_phys));
        return (KernelAddress) {
            .virtual_addr = 0,
            .phys_addr = 0,
            .valid = false
        };
    }

    return (KernelAddress) {
        .virtual_addr = virtual,
        .phys_addr = physical,
        .valid = true
    };
}

KernelAddress MemoryPage__from_physical_address(Address physical)
{
    Address offset = physical - addr_of(__heap_start_phys);
    Address virtual = offset + addr_of(__heap_start);

    if (physical < addr_of(__heap_start_phys)) {
        __d_printf(PANIC, "Unknown physical addr=%p", physical);
        // is it one of our allocated addresses?
        return (KernelAddress) {
            .virtual_addr = 0,
            .phys_addr = 0,
            .valid = false
        };
    }

    return (KernelAddress) {
        .virtual_addr = virtual,
        .phys_addr = physical,
        .valid = true
    };
}

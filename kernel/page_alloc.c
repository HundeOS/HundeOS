#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "page_alloc.h"
#include "debug.h"
#include "mutex.h"
#include "panic.h"

#define _1MB (1024 * 1024)
#define _2MB (2 * 1024 * 1024)
#define _1GB (1024 * _1MB)
#define COARSE_BLOCK_SIZE (4 * _1MB)
#define PAGES_PER_BLOCK (COARSE_BLOCK_SIZE / PAGE_SIZE)

#define _4GB = 4 * _1GB;

extern char __heap_start[];
extern char __heap_end[];
extern char __heap_start_phys[];

// later we can avoid most locks by having different blocks per cpu
static mutex_t block_lock = MUTEX_INIT;
static mutex_t alloc_lock;

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

inline MemoryPage * align_up(MemoryPage * page, uint64_t a){
    uint64_t x = (uint64_t)page;
    return (MemoryPage *)((x + (a - 1)) & ~(a - 1));
}

static void MemoryBlock__advance() {
    Address next_phys_addr = current_block.start.phys_addr + COARSE_BLOCK_SIZE;
    Address next_virtual_addr = current_block.start.phys_addr + COARSE_BLOCK_SIZE;

    if (next_virtual_addr > addr_of(__heap_end)) {
        Kernel__panic("out of heap", NULL);
    }

    current_block.pages_allocated = 0;
    current_block.start.phys_addr = next_phys_addr;
    current_block.start.virtual_addr = next_virtual_addr;
}

static void MemoryBlock__bootstrap(){
    __d_println(FOCUS, "MemoryBlock__bootstrap");
    // we need to allocate the first block
    current_block.start.phys_addr = addr_of(__heap_start_phys); // work out if there is an offset
    current_block.start.virtual_addr = addr_of(__heap_start);
    current_block.pages_allocated = 0;
    current_block.bootstrapped = true;

    __d_printf(FOCUS, "start_phys=%p, start_virt=%p\n",current_block.start.phys_addr, current_block.start.virtual_addr);
}


static MemoryPage * MemoryBlock__next_page() {
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
    MemoryPage * page = (MemoryPage *)current_block.start.virtual_addr + (page_index * PAGE_SIZE);
    __d_printf(DEBUG, "MemoryBlock__next_page 0x%x\n", page);

    return page;
}

static MemoryPage * MemoryPage__next() {
    return MemoryBlock__next_page();
}

MemoryPage * MemoryPage__alloc_fresh() {
    MemoryPage * page = MemoryPage__next();
    return MemoryPage__zero(page);;
}

MemoryPage * MemoryPage__alloc(void)
{
    __d_println(DEBUG, "alloc");
    return MemoryPage__alloc_fresh();
}

KernelAddress MemoryPage__address(MemoryPage * self)
{
    Address virtual = addr(self);
    Address offset = virtual - addr_of(__heap_start);
    Address physical = offset + addr_of(__heap_start_phys);

    if (addr(self) < addr_of(__heap_start)) {
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

#include "page_alloc.h"
#include "../kernel/kernel.h"
#include "page_alloc.h"
#include <stdint.h>
#include <stddef.h>

#include "page_table.h"

const uint64_t _4GB = 4294967296;


extern char __heap_start[];
extern char __heap_end[];
extern char __heap_start_phys[];

__BOOTSTRAP__(char) bootstrap[_2MB];
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

static mutex_t alloc_lock;


static void MemoryPage__init(){
    __d_println(FOCUS, "MemoryPage__init");
    // we need to allocate the first block
    uint64_t phys = (Address) &__heap_start_phys;
    uint64_t virtual = (Address) &__heap_start;

    current_block.start.phys_addr = phys + ((Address)&bootstrap - virtual); // work out if there is an offset
    current_block.start.virtual_addr = (Address) &bootstrap;
    current_block.pages_allocated = 0;
    current_block.bootstrapped = true;

    __d_printf(FOCUS, "start_phys=0x%x, start_virt=0x%x\n",
        current_block.start.phys_addr, current_block.start.virtual_addr);
}

static void MemoryPage__advance_block() {
    Address next_phys_addr = current_block.start.phys_addr + COARSE_BLOCK_SIZE;
    Address next_virtual_addr = current_block.start.phys_addr + COARSE_BLOCK_SIZE;
    if (next_virtual_addr > (Address) &__heap_end) {
        panic("out of heap", NULL);
    }
    current_block.pages_allocated = 0;
    current_block.start.phys_addr = next_phys_addr;
    current_block.start.virtual_addr = next_virtual_addr;
}

static MemoryPage * MemoryPage__next() {
    uint64_t page_index = current_block.pages_allocated;
    if (page_index >= PAGES_PER_BLOCK) {
        MemoryPage__advance_block();
    }
    current_block.pages_allocated++;
    return (MemoryPage *)current_block.start.virtual_addr + (page_index * PAGE_SIZE);
}

MemoryPage * MemoryPage__alloc_fresh() {
    MemoryPage * page = NULL;
    // Serialize access to the simple bump-pointer allocator.
    mutex_lock(&alloc_lock);

    // Lazy-initialize the allocator state on first use.
    if (!current_block.bootstrapped) {
        MemoryPage__init();  // sets g_page_cursor/limit to pool bounds (4 KiB aligned)
    }

    page = MemoryPage__next();
    __d_printf(DEBUG, "MemoryPage_alloc_fresh 0x%x\n", page);
    mutex_unlock(&alloc_lock);

    __d_printf(DEBUG, "MemoryPage_alloc_fresh 0x%x\n", page);
    return MemoryPage__zero(page);;
}

MemoryPage * MemoryPage__alloc(void)
{
    __d_println(DEBUG, "alloc");
    return MemoryPage__alloc_fresh();
    __d_println(DEBUG, "alloc complete");
}

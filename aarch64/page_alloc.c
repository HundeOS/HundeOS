#include "page_alloc.h"
#include "../kernel/kernel.h"
#include <stdint.h>
#include <stddef.h>

const uint64_t _4GB = 4294967296;

static MemoryPage *g_page_cursor = NULL;
static MemoryPage *g_page_limit  = NULL;

static FreeMemoryPage * free = NULL;

inline MemoryPage * align_up(MemoryPage * page, uint64_t a){
    uint64_t x = (uint64_t)page;
    return (MemoryPage *)((x + (a - 1)) & ~(a - 1));
}

static mutex_t alloc_lock;
static mutex_t free_lock;

inline MemoryPage * MemoryPage__zero(MemoryPage * self){
    for (int i = 0; i < PAGE_SIZE; i++) {
        self->page[i] = 0;
    }

    __asm__ __volatile__("dsb ish" ::: "memory");
    return self;
}

static void MemoryPage__init(){
    g_page_cursor = align_up((MemoryPage *)__heap_start, PAGE_SIZE);
    g_page_limit = align_up((MemoryPage *)(__heap_start + _4GB), PAGE_SIZE);
    __d_printf(DEBUG, "g_page_cursor=0x%x, g_page_limit=0x%x, unaligned=0x%x\n", g_page_cursor, g_page_limit, _4GB);
}

MemoryPage * MemoryPage__next_free()
{
    __d_println(DEBUG, "MemoryPage__next_free");
    FreeMemoryPage * page = NULL;
    mutex_lock(&free_lock);
    __d_println(DEBUG, "mutex");
    if (free) {
        page = free;
        free = free->next;
    }
    mutex_unlock(&free_lock);
    // zero the free pointer
    if (page) {
        page->next = NULL;
    }
    return (MemoryPage *) page;
}

void MemoryPage__free(MemoryPage * page)
{
    // Serialize access to the simple bump-pointer allocator.
    mutex_lock(&free_lock);

    FreeMemoryPage * freed = (FreeMemoryPage *) MemoryPage__zero(page);
    freed->next = free;
    free = freed;
    mutex_unlock(&free_lock);
}

MemoryPage * MemoryPage_alloc_fresh() {
    MemoryPage * page = NULL;
    // Serialize access to the simple bump-pointer allocator.
    mutex_lock(&alloc_lock);

    // Lazy-initialize the allocator state on first use.
    if (!g_page_cursor) {
        MemoryPage__init();  // sets g_page_cursor/limit to pool bounds (4 KiB aligned)
    }
    page = align_up(g_page_cursor, PAGE_SIZE);
    __d_printf(DEBUG, "MemoryPage_alloc_fresh 0x%x\n", page);
    // Out-of-memory check: would advancing by one page exceed the pool?
    if (page + PAGE_SIZE > g_page_limit) {
        mutex_unlock(&alloc_lock);
        return NULL;
    }
    // Advance the bump pointer to the next free position.
    g_page_cursor = page + PAGE_SIZE;
    mutex_unlock(&alloc_lock);
    MemoryPage__zero(page);             // 4096 bytes of zero; includes a DSB for visibility if your impl does
    __d_printf(DEBUG, "MemoryPage_alloc_fresh 0x%x\n", page);
    return page;
}

MemoryPage * MemoryPage__alloc(void)
{
    __d_println(DEBUG, "alloc");
    MemoryPage * page = MemoryPage__next_free();
    __d_println(DEBUG, "free");
    // Grab the next 4 KiB slot, rounding the cursor up to a page boundary.
    if (!page) {
        __d_println(DEBUG, "fresh");
        page = MemoryPage_alloc_fresh();
    }
    __d_println(DEBUG, "alloc complete");
    return page;
}

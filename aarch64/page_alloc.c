#include "page_alloc.h"
#include "../kernel/kernel.h"
#include <stdint.h>
#include <stddef.h>

static uint8_t *g_page_cursor = NULL;
static uint8_t *g_page_limit  = NULL;

inline uintptr_t align_up(uintptr_t x, uintptr_t a){
    return (x + (a - 1)) & ~(a - 1);
}

static mutex_t alloc_lock;
#define WORDS \
    (int)(PAGE_SIZE / sizeof(uint64_t))

inline void MemoryPage__zero(MemoryPage self){
    for (int i = 0; i < WORDS; i++) {
        self[i] = 0;
    }

    __asm__ __volatile__("dsb ish" ::: "memory");
}

static void MemoryPage__init(){
    g_page_cursor = (uint8_t *)align_up((uintptr_t)__page_pool_start, PAGE_SIZE);
    g_page_limit  = (uint8_t *)((uintptr_t)__page_pool_end & ~((uintptr_t)0xFFF));
}

MemoryPage MemoryPage__alloc(void)
{
    // Serialize access to the simple bump-pointer allocator.
    mutex_lock(&alloc_lock);

    // Lazy-initialize the allocator state on first use.
    if (!g_page_cursor) {
        MemoryPage__init();  // sets g_page_cursor/limit to pool bounds (4 KiB aligned)
    }

    // Grab the next 4 KiB slot, rounding the cursor up to a page boundary.
    uintptr_t cur = align_up((uintptr_t)g_page_cursor, PAGE_SIZE);

    // Out-of-memory check: would advancing by one page exceed the pool?
    if (cur + PAGE_SIZE > (uintptr_t)g_page_limit) {
        mutex_unlock(&alloc_lock);
        return NULL;
    }

    // Advance the bump pointer to the next free position.
    g_page_cursor = (uint8_t *)(cur + PAGE_SIZE);

    // Release the allocator lock before zeroing (zeroing is private to this page).
    mutex_unlock(&alloc_lock);

    // Clear the page contents so new users never see stale data.
    MemoryPage page = (MemoryPage) cur;
    MemoryPage__zero(page);             // 4096 bytes of zero; includes a DSB for visibility if your impl does

    return page;
}

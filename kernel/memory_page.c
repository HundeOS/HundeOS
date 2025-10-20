#include "memory_page.h"
#include "all_defs.h"
#include <stdint.h>


MemoryPagePtr MemoryPage__zero(MemoryPagePtr self)
{
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        self->page[i] = 0;
    }
    return self;
}

#include "memory_page.h"
#include "all_defs.h"
#include "memory.h"
#include <stdint.h>

MemoryPagePtr MemoryPage__zero(MemoryPagePtr self)
{
    return Mem__memset(self, 0, PAGE_SIZE);
}

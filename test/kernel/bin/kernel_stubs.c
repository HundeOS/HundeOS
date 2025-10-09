#include <stdlib.h>

#include "../../../kernel/mem.h"
#include "../../../kernel/page_alloc.h"

MemoryPage * MemoryPage__alloc()
{
    return malloc(PAGE_SIZE);
}

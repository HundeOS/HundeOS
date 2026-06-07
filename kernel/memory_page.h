#ifndef __KERNEL_PAGE_ALLOC_H_
#define __KERNEL_PAGE_ALLOC_H_

#include "all_defs.h"

typedef struct MemoryPage
{
    char page[PAGE_SIZE];
}
MemoryPage;

typedef MemoryPage * MemoryPagePtr;

MemoryPagePtr MemoryPage__zero(MemoryPagePtr self);

#endif
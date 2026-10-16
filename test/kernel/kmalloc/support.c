#include <stdlib.h>

#include "../../../kernel/kalloc.h"

void CPU__yield(void)
{
}

CPUID CPU__id(void)
{
    return 0;
}

KernelAddress Kernel__alloc(size_t size)
{
    size_t aligned_size = (size + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
    void * allocation = aligned_alloc(PAGE_SIZE, aligned_size);
    if (!allocation)
    {
        return INVALID_KERNEL_ADDR;
    }
    return KERNEL_ADDR(addr(allocation), addr(allocation));
}

KernelAddress Kernel__alloc_page(void)
{
    return Kernel__alloc(PAGE_SIZE);
}

Result(KAllocCode) Kernel__free(Address virtual_address)
{
    free(void_ptr(virtual_address));
    return Success(KAllocCode, KALLOC_OK);
}

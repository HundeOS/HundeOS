#include "loader.h"

#include "../sys/mem.h"

WuffMemoryOperations WuffMemoryOperations__new(void * context, WuffAllocateAligned allocate_aligned,
                                               WuffProtect protect, WuffFree free)
{
    return (WuffMemoryOperations){
        .context = context,
        .allocate_aligned = allocate_aligned,
        .protect = protect,
        .free = free,
    };
}

static Result(WuffMemoryRegion) WuffMemory__allocate_aligned(void * context, size_t size, size_t alignment)
{
    match_result(
        mem_alloc_aligned_pages_result(size, alignment),
        success(value)
        {
            Address address = (Address)value;
            return Success(WuffMemoryRegion, WuffMemoryRegion__new(address, (uint8_t *)address));
        },
        error(code, message)
        {
            return Error(WuffMemoryRegion, message, code);
        }
    );
    return Error(WuffMemoryRegion, bstr("Unable to allocate aligned loader memory"), MEM_ALLOCATION_FAILED);
}

static MemStatus WuffMemory__protect(void * context, Address address, size_t size, MemPagePermissions permissions)
{
    return mem_protect_pages((void *)address, size, permissions);
}

static MemStatus WuffMemory__free(void * context, Address address)
{
    return mem_free_pages((void *)address);
}

WuffMemoryOperations WuffMemoryOperations__system(void)
{
    return WuffMemoryOperations__new(0, WuffMemory__allocate_aligned, WuffMemory__protect, WuffMemory__free);
}

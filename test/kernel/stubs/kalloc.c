#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../../kernel/address.h"
#include "../../../kernel/debug.h"
#include "../../../kernel/kalloc.h"

static bool virtual_translation_enabled = false;
static Address virtual_translation_start = 0;
static Address virtual_translation_end = 0;
static Address virtual_translation_physical_start = 0;
static bool alloc_fill_enabled = false;
static uint8_t alloc_fill_value = 0;
static bool user_alloc_failure_enabled = false;
static bool free_failure_enabled = false;
static bool memcopy_overflow_occurred = false;
static size_t alloc_count = 0;
static size_t free_count = 0;

#define KALLOC_STUB_MAX_TRACKED_ALLOCATIONS 16384

typedef struct KAllocStubAllocation
{
    Address address;
    size_t size;
    bool active;
    bool freed;
} KAllocStubAllocation;

static KAllocStubAllocation tracked_allocations[KALLOC_STUB_MAX_TRACKED_ALLOCATIONS];
static size_t tracked_allocation_count = 0;

static KAllocStubAllocation KAllocStubAllocation__new(Address address, size_t size)
{
    return (KAllocStubAllocation){
        .address = address,
        .size = size,
        .active = true,
        .freed = false,
    };
}

static void KAllocStub__track_allocation(Address address, size_t size)
{
    if (tracked_allocation_count < KALLOC_STUB_MAX_TRACKED_ALLOCATIONS)
    {
        tracked_allocations[tracked_allocation_count] = KAllocStubAllocation__new(address, size);
        tracked_allocation_count++;
    }
}

static void KAllocStub__track_free(Address address)
{
    for (size_t index = tracked_allocation_count; index > 0; index--)
    {
        KAllocStubAllocation * allocation = &tracked_allocations[index - 1];
        if (allocation->active && allocation->address == address)
        {
            allocation->active = false;
            allocation->freed = true;
            return;
        }
    }
}

void KAllocStub__reset_virtual_translation(void)
{
    virtual_translation_enabled = false;
    virtual_translation_start = 0;
    virtual_translation_end = 0;
    virtual_translation_physical_start = 0;
}

void KAllocStub__set_virtual_translation(Address virtual_start, size_t size, Address physical_start)
{
    virtual_translation_enabled = true;
    virtual_translation_start = virtual_start;
    virtual_translation_end = virtual_start + size;
    virtual_translation_physical_start = physical_start;
}

void KAllocStub__set_alloc_fill(bool enabled, uint8_t value)
{
    alloc_fill_enabled = enabled;
    alloc_fill_value = value;
}

void KAllocStub__set_user_alloc_failure(bool enabled)
{
    user_alloc_failure_enabled = enabled;
}

void KAllocStub__reset_allocation_tracking(void)
{
    memcopy_overflow_occurred = false;
    user_alloc_failure_enabled = false;
    alloc_count = 0;
    free_count = 0;
    tracked_allocation_count = 0;
}

size_t KAllocStub__alloc_count(void)
{
    return alloc_count;
}

size_t KAllocStub__free_count(void)
{
    return free_count;
}

bool KAllocStub__is_allocated(Address address)
{
    for (size_t index = tracked_allocation_count; index > 0; index--)
    {
        if (tracked_allocations[index - 1].address == address)
        {
            return tracked_allocations[index - 1].active;
        }
    }
    return false;
}

size_t KAllocStub__freed_allocation_count_for_size(size_t size)
{
    size_t count = 0;
    for (size_t index = 0; index < tracked_allocation_count; index++)
    {
        if (tracked_allocations[index].freed && tracked_allocations[index].size == size)
        {
            count++;
        }
    }
    return count;
}

bool KAllocStub__memcopy_overflow_occurred(void)
{
    return memcopy_overflow_occurred;
}

KernelAddress Kernel__alloc(size_t size)
{
    alloc_count++;
    Address addr = (Address)malloc(size);
    KAllocStub__track_allocation(addr, size);
    if (alloc_fill_enabled)
    {
        uint8_t * data = ptr(addr, uint8_t);
        for (size_t index = 0; index < size; index++)
        {
            data[index] = alloc_fill_value;
        }
    }
    __d_printf(DEBUG, "Kernel__alloc(size) = %p\n", addr);
    return KERNEL_ADDR(addr, addr);
}

KernelAddress Kernel__alloc_page(void)
{
    return Kernel__alloc(PAGE_SIZE);
}

KernelAddress Kernel__alloc_block(void)
{
    return Kernel__alloc(SystemInfo__default_heap_coarse_block_size());
}

KernelAddress Kernel__from_virtual_address(KernelPtr self, Address virtual_addr)
{
    if (virtual_translation_enabled && virtual_addr >= virtual_translation_start &&
        virtual_addr < virtual_translation_end)
    {
        return KERNEL_ADDR(virtual_addr,
                           virtual_translation_physical_start + (virtual_addr - virtual_translation_start));
    }

    return KERNEL_ADDR(virtual_addr, virtual_addr);
}

KernelAddress Kernel__try_from_virtual_address(KernelPtr self, Address virtual_addr)
{
    return Kernel__from_virtual_address(self, virtual_addr);
}

KernelAddress Kernel__from_physical_address(KernelPtr self, Address physical)
{
    return KERNEL_ADDR(physical, physical);
}

Address Kernel__alloc_kernel(size_t size)
{
    return vaddr(Kernel__alloc(size));
}

UserAddress Kernel__alloc_user(Address virtual_address, size_t size, bool read, bool write, bool execute)
{
    KernelAddress address = Kernel__alloc(size);

    return UserAddress__new(virtual_address, address, read, write, execute, true);
}

Result(UserAddress) Kernel__try_alloc_user(Address virtual_address, size_t size, bool read, bool write, bool execute)
{
    if (user_alloc_failure_enabled)
    {
        return Error(UserAddress, bstr("Allocation failed"), KALLOC_ALLOCATION_FAILED);
    }

    return Success(UserAddress, Kernel__alloc_user(virtual_address, size, read, write, execute));
}

Result(UserAddress)
    Kernel__try_alloc_reclaimable_user(Address virtual_address, size_t size, bool read, bool write, bool execute)
{
    return Kernel__try_alloc_user(virtual_address, size, read, write, execute);
}

Result(UserAddress) Kernel__try_alloc_user_aligned(Address virtual_address, size_t size, size_t alignment, bool read,
                                                   bool write, bool execute)
{
    return Kernel__try_alloc_user(virtual_address, size, read, write, execute);
}

Result(KAllocCode) Kernel__free(Address virtual_address)
{
    if (free_failure_enabled)
    {
        return Error(KAllocCode, bstr("Free failed"), KALLOC_INVALID_ADDRESS);
    }
    free_count++;
    KAllocStub__track_free(virtual_address);
    free(void_ptr(virtual_address));
    return Success(KAllocCode, KALLOC_OK);
}

__attribute__((weak)) KernelAddress Kernel__malloc(size_t size)
{
    return Kernel__alloc(size);
}

__attribute__((weak)) Result(KAllocCode) Kernel__free_malloc(Address allocation)
{
    return Kernel__free(allocation);
}

void KAllocStub__set_free_failure(bool enabled)
{
    free_failure_enabled = enabled;
}

void Kernel__memcopy(Address src, Address target, size_t size)
{
    void * v_src = (void *)src;
    void * v_target = (void *)target;

    for (int i = 0; i < size; i++)
    {
        ((char *)v_target)[i] = ((char *)v_src)[i];
    }
}

void * memset(void * dst, int c, size_t n)
{
    uint8_t * d = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;
    while (n--)
    {
        *d++ = v;
    }
    return dst;
}

void Kernel__memset(Address addr, int val, size_t size)
{
    memset(void_ptr(addr), val, size);
}

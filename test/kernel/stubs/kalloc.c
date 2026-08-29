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
static bool memcopy_overflow_occurred = false;

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

void KAllocStub__reset_allocation_tracking(void)
{
    memcopy_overflow_occurred = false;
}

bool KAllocStub__memcopy_overflow_occurred(void)
{
    return memcopy_overflow_occurred;
}

KernelAddress Kernel__alloc(size_t size)
{
    Address addr = (Address) malloc(size);
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

KernelAddress Kernel__from_virtual_address(KernelPtr kernel, Address virtual_addr)
{
    if (virtual_translation_enabled && virtual_addr >= virtual_translation_start && virtual_addr < virtual_translation_end)
    {
        return KERNEL_ADDR(virtual_addr, virtual_translation_physical_start + (virtual_addr - virtual_translation_start));
    }

    return KERNEL_ADDR(virtual_addr, virtual_addr);
}

KernelAddress Kernel__try_from_virtual_address(KernelPtr kernel, Address virtual_addr)
{
    return Kernel__from_virtual_address(kernel, virtual_addr);
}

KernelAddress Kernel__from_physical_address(KernelPtr kernel, Address physical)
{
    return KERNEL_ADDR(physical, physical);
}

Address Kernel__alloc_kernel(size_t size)
{
    return vaddr(Kernel__alloc(size));
}

UserAddress Kernel__alloc_user(Address virtual_address,
                               size_t size,
                               bool read,
                               bool write,
                               bool execute)
{
    KernelAddress address = Kernel__alloc(size);

    return (UserAddress) {
        .virtual_addr = virtual_address,
        .kernel_addr = address,
        .read = read,
        .write = write,
        .execute = execute,
        .valid = true,
    };
}

Result(KAllocCode) Kernel__free(Address virtual_address)
{
    free(void_ptr(virtual_address));
    return Success(KAllocCode, KALLOC_OK);
}

void Kernel__memcopy(Address src, Address target, size_t size)
{
    void * v_src = (void *)src;
    void * v_target = (void *)target;

    for (int i = 0; i < size; i++) {
        ((char *)v_target)[i] = ((char *)v_src)[i];
    }
}

void * memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;
    while (n--) { *d++ = v; }
    return dst;
}

void Kernel__memset(Address addr, int val, size_t size)
{
    memset(void_ptr(addr), val, size);
}

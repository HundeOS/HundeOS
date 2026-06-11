#include "kalloc.h"

#include <stdint.h>

#include "debug.h"
#include "address.h"
#include "kernel_core.h"

static KString not_implemented = KSTRING_NEW("Not implemented");
static KString invalid_address = KSTRING_NEW("Invalid address");

extern char __text_virt[];
extern char __text_phys[];
extern char __lma_end[];

inline HeapPtr heap_ptr()
{
    KernelPtr kernel = Kernel__instance();
    return &kernel->heap;
}

static bool Kernel__is_from_high_kernel_virtual(Address virtual_addr)
{
    Address virtual_start = addr_of(__text_virt);
    Address physical_start = addr_of(__text_phys);
    Address physical_end = addr_of(__lma_end);
    Address virtual_end = virtual_start + (physical_end - physical_start);

    return virtual_addr >= virtual_start && virtual_addr < virtual_end;
}

static bool Kernel__is_from_high_kernel_physical(Address physical)
{
    Address physical_start = addr_of(__text_phys);
    Address physical_end = addr_of(__lma_end);

    return physical >= physical_start && physical < physical_end;
}

KernelAddress Kernel__from_physical_address(Address physical)
{
    if (Kernel__is_from_high_kernel_physical(physical))
    {
        Address virtual_start = addr_of(__text_virt);
        Address physical_start = addr_of(__text_phys);
        return KERNEL_ADDR(virtual_start + (physical - physical_start), physical);
    }

    return Heap__from_physical_address(heap_ptr(), physical);
}

KernelAddress Kernel__from_virtual_address(Address virtual_addr)
{
    if (Kernel__is_from_high_kernel_virtual(virtual_addr))
    {
        Address virtual_start = addr_of(__text_virt);
        Address physical_start = addr_of(__text_phys);
        return KERNEL_ADDR(virtual_addr, physical_start + (virtual_addr - virtual_start));
    }

    return Heap__from_virtual_address(heap_ptr(), virtual_addr);
}

KernelAddress Kernel__alloc(size_t size)
{
    return Heap__alloc_size(heap_ptr(), size);
}

Address Kernel__alloc_kernel(size_t size)
{
    KernelAddress address = Kernel__alloc(size);

    return address.virtual_addr;
}

UserAddress Kernel__alloc_user(Address virtual_address,
                               size_t size,
                               bool read,
                               bool write,
                               bool execute)
{
    __d_println(DEBUG, "Kernel__alloc(size)");
    KernelAddress address = Kernel__alloc(size);
    UserAddress allocated = (UserAddress) {
        .virtual_addr = virtual_address,
        .kernel_addr = address,
        .read = read,
        .write = write,
        .execute = execute,
        .valid = true,
    };
    return allocated;
}

Result(KAllocCode) Kernel__free(Address virtual_address)
{
    KernelAddress address = Kernel__from_virtual_address(virtual_address);
    if (!address.valid)
    {
        return Error(KAllocCode, invalid_address, KALLOC_INVALID_ADDRESS);
    }

    ResultMatch(HeapCode, Heap__free(heap_ptr(), address))
        IsSuccess(_, return Success(KAllocCode, KALLOC_OK));
        IsError(code, message, return Error(KAllocCode, message, code));
    End

    return Error(KAllocCode, not_implemented, KALLOC_NOT_IMPLEMENTED);
}

void * memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void * memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint8_t v = (uint8_t)c;
    while (n--) { *d++ = v; }
    return dst;
}

void Kernel__memcopy(Address src, Address target, size_t size)
{
    void * v_src = (void *)src;
    void * v_target = (void *)target;

    for (int i = 0; i < size; i++) {
        ((char *)v_target)[i] = ((char *)v_src)[i];
    }
}

void Kernel__memset(Address addr, int val, size_t size)
{
    memset(void_ptr(addr), val, size);
}

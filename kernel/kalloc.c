#include "kalloc.h"

#include <stdint.h>

#include "debug.h"
#include "address.h"
#include "kernel_core.h"

static BString not_implemented = BSTRING_NEW("Not implemented");
static BString invalid_address = BSTRING_NEW("Invalid address");

inline HeapPtr heap_ptr()
{
    KernelPtr kernel = Kernel__instance();
    return &kernel->heap;
}

KernelAddress Kernel__from_physical_address(KernelPtr self, Address physical)
{
    match_optional(SystemInfo, self->system_info,
        some(system_info)
        {
            KernelAddress address = SystemInfo__kernel_address_for_physical(&system_info, physical);
            if (address.valid)
            {
                return address;
            }
        },
        none() {}
    )

    return Heap__from_physical_address(heap_ptr(), physical);
}

KernelAddress Kernel__from_virtual_address(KernelPtr self, Address virtual_addr)
{
    match_optional(SystemInfo, self->system_info,
        some(system_info)
        {
            KernelAddress address = SystemInfo__kernel_address_for_virtual(&system_info, virtual_addr);
            if (address.valid)
            {
                return address;
            }
        },
        none() {}
    )

    return Heap__from_virtual_address(heap_ptr(), virtual_addr);
}

KernelAddress Kernel__try_from_virtual_address(KernelPtr self, Address virtual_addr)
{
    match_optional(SystemInfo, self->system_info,
        some(system_info)
        {
            KernelAddress address = SystemInfo__kernel_address_for_virtual(&system_info, virtual_addr);
            if (address.valid)
            {
                return address;
            }
        },
        none() {}
    )

    return Heap__try_from_virtual_address(heap_ptr(), virtual_addr);
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
    KernelAddress address = Kernel__from_virtual_address(Kernel__instance(), virtual_address);
    if (!address.valid)
    {
        return Error(KAllocCode, invalid_address, KALLOC_INVALID_ADDRESS);
    }

    match_result(HeapCode, Heap__free(heap_ptr(), address),
        success(_)
        {
            return Success(KAllocCode, KALLOC_OK);
        },
        error(code, message) { return Error(KAllocCode, message, code); }
    )

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

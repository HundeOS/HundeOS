#include "kalloc.h"

#include <stdint.h>

#include "debug.h"
#include "address.h"
#include "kernel_core.h"

inline HeapPtr heap_ptr()
{
    KernelPtr kernel = Kernel__instance();
    return &kernel->heap;
}

KernelAddress Kernel__from_physical_address(Address physical)
{
    return Heap__from_physical_address(heap_ptr(), physical);
}

KernelAddress Kernel__from_virtual_address(Address virtual_addr)
{
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
    };
    return allocated;
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


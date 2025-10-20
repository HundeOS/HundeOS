#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../../kernel/address.h"

KernelAddress Kernel__alloc(size_t size)
{
    Address addr = (Address) malloc(size);
    return KERNEL_ADDR(addr, addr);
}

Address Kernel__alloc_kernel(size_t size)
{
    return Kernel__alloc(size).virtual_addr;
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


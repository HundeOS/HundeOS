#ifndef __KERNEL_ADDRESS_H
#define __KERNEL_ADDRESS_H

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t Address;
typedef uint64_t Offset;

#define addr_of(p) addr(&p)
#define vaddr(ka) ka.virtual_addr
#define paddr(ka) ka.phys_addr
#define u_kpaddr(ua) ua.kernel_addr.phys_addr
#define u_kvaddr(ua) ua.kernel_addr.virtual_addr
#define offset_addr(a, b) a + b

inline Address addr(void * p) {
    return (Address) p;
}

typedef struct KernelAddress {
    Address virtual_addr;
    Address phys_addr;
    bool valid;
} KernelAddress;

typedef struct UserAddress
{
    Address virtual_addr;
    KernelAddress kernel_addr;
    bool read;
    bool write;
    bool execute;
    bool valid;
} UserAddress;

#define NULL_KERNEL_ADDRESS (KernelAddress) { \
    .virtual_addr = 0, \
    .phys_addr = 0, \
    .valid = false, \
}

#define NULL_USER_ADDRESS (UserAddress) { \
    .virtual_addr = 0, \
    .kernel_addr = NULL_KERNEL_ADDRESS, \
    .read = false, \
    .write = false, \
    .execute = false, \
    .valid = false, \
}

#define v_ptr(a, type) ((type *) a.virtual_addr)
#define void_v_ptr(a) ((void *) a.virtual_addr)

#define p_ptr(a, type) ((type *) a.phys_addr)
#define void_p_ptr(a) ((void *) a.phys_addr)

#define k_v_ptr(a, type) ((type *) a.kernel_addr.virtual_addr)
#define void_k_v_ptr(a) ((void *) a.kernel_addr.virtual_addr)

#define k_p_ptr(a, type) ((type *) a.kernel_addr.phys_addr)
#define void_k_p_ptr(a) ((void *) a.kernel_addr.phys_addr)

#define ptr(a, type) ((type *) a)
#define void_ptr(a) ((void *) a)

#define offset_ptr(a, o, type) ((type *) (a + o))
#define offset_void_ptr(a, o) ((void *) (a + o))

inline UserAddress UserAddress__relative(UserAddress * self, Offset offset)
{
    return (UserAddress) {
        .virtual_addr = self->virtual_addr + offset,
        .kernel_addr = (KernelAddress) {
            .virtual_addr = self->kernel_addr.virtual_addr + offset,
            .phys_addr = self->kernel_addr.phys_addr + offset,
            .valid = self->kernel_addr.valid,
        },
        .read = self->read,
        .write = self->write,
        .execute = self->execute,
        .valid = self->valid,
    };
}

#endif
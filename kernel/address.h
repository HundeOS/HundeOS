#ifndef __KERNEL_ADDRESS_H
#define __KERNEL_ADDRESS_H

#include <stdint.h>
#include <stdbool.h>

#include "../common/types/address.h"
#include "types/result.h"

ResultDef(Address);

#define addr_of(p) addr(&p)
#define vaddr(ka) ka.virtual_addr
#define paddr(ka) ka.phys_addr
#define u_kpaddr(ua) ua.kernel_addr.phys_addr
#define u_kvaddr(ua) ua.kernel_addr.virtual_addr
#define offset_addr(a, b) (Address)(a + b)

inline Address addr(void * p)
{
    return (Address)p;
}

typedef struct KernelAddress
{
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

typedef KernelAddress * KernelAddressPtr;
typedef UserAddress * UserAddressPtr;
typedef const UserAddress * ConstUserAddressPtr;

ResultDef(UserAddress);

inline KernelAddress KernelAddress__new(Address virtual_addr, Address phys_addr, bool valid)
{
    return (KernelAddress){
        .virtual_addr = virtual_addr,
        .phys_addr = phys_addr,
        .valid = valid,
    };
}

inline UserAddress UserAddress__new(Address virtual_addr, KernelAddressPtr kernel_addr, bool read, bool write,
                                    bool execute, bool valid)
{
    return (UserAddress){
        .virtual_addr = virtual_addr,
        .kernel_addr = *kernel_addr,
        .read = read,
        .write = write,
        .execute = execute,
        .valid = valid,
    };
}

#define UserAddress__new(virtual_addr, kernel_addr, read, write, execute, valid)                                       \
    ({                                                                                                                 \
        KernelAddress __kernel_addr = (kernel_addr);                                                                   \
        UserAddress__new((virtual_addr), &__kernel_addr, (read), (write), (execute), (valid));                         \
    })

#define KERNEL_ADDR(virt, phys)                                                                                        \
    (KernelAddress)                                                                                                    \
    {                                                                                                                  \
        .virtual_addr = virt, .phys_addr = phys, .valid = true,                                                        \
    }

#define INVALID_KERNEL_ADDR                                                                                            \
    (KernelAddress)                                                                                                    \
    {                                                                                                                  \
        .virtual_addr = 0, .phys_addr = 0, .valid = false,                                                             \
    }

#define NULL_KERNEL_ADDRESS INVALID_KERNEL_ADDR
#define NULL_USER_ADDRESS                                                                                              \
    (UserAddress)                                                                                                      \
    {                                                                                                                  \
        .virtual_addr = 0, .kernel_addr = NULL_KERNEL_ADDRESS, .read = false, .write = false, .execute = false,        \
        .valid = false,                                                                                                \
    }

#define v_ptr(a, type) ((type *)a.virtual_addr)
#define void_v_ptr(a) ((void *)a.virtual_addr)

#define p_ptr(a, type) ((type *)a.phys_addr)
#define void_p_ptr(a) ((void *)a.phys_addr)

#define k_v_ptr(a, type) ((type *)a.kernel_addr.virtual_addr)
#define void_k_v_ptr(a) ((void *)a.kernel_addr.virtual_addr)

#define k_p_ptr(a, type) ((type *)a.kernel_addr.phys_addr)
#define void_k_p_ptr(a) ((void *)a.kernel_addr.phys_addr)

#define ptr(a, type) ((type *)a)
#define void_ptr(a) ((void *)a)

#define offset_ptr(a, o, type) ((type *)(a + o))
#define offset_void_ptr(a, o) ((void *)(a + o))

inline UserAddress UserAddress__relative(UserAddressPtr self, Offset offset)
{
    KernelAddress kernel_addr = KernelAddress__new(self->kernel_addr.virtual_addr + offset,
                                                   self->kernel_addr.phys_addr + offset, self->kernel_addr.valid);
    return UserAddress__new(self->virtual_addr + offset, kernel_addr, self->read, self->write, self->execute,
                            self->valid);
}

bool UserAddress__equals(UserAddressPtr self, UserAddressPtr other);
bool KernelAddress__equals(KernelAddressPtr self, KernelAddressPtr other);

#endif

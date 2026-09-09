#ifndef __KERNEL_AARCH64_MMIO_HELPERS_H_
#define __KERNEL_AARCH64_MMIO_HELPERS_H_

#include <stdint.h>

inline uint32_t mmio_read32(const volatile void * addr)
{
    return *(const volatile uint32_t *)addr;
}

inline void mmio_write32(uint32_t v, volatile void * addr)
{
    *(volatile uint32_t *)addr = v;
}

inline uint32_t r32(uintptr_t a)
{
    return *(volatile uint32_t *)a;
}

inline void w32(uintptr_t a, uint32_t v)
{
    *(volatile uint32_t *)a = v;
}

#endif
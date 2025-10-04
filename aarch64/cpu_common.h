#ifndef __AARCH64_CPU_COMMON_H_
#define __AARCH64_CPU_COMMON_H_
#include "asm_helpers.h"

#define MAX_CPUS 16
#define STACK_SIZE (16*1024)

inline uint64_t cpu_id() {
    return read_tpidr_el1();
}

// Simple helper: on QEMU virt, MPIDR.AFF0 == logical cpu#
inline uint64_t mpidr_pack(unsigned aff3, unsigned aff2, unsigned aff1, unsigned aff0)
{
    return ((uint64_t)aff3 << 32) | ((uint64_t)aff2 << 16) | ((uint64_t)aff1 << 8) | (uint64_t)aff0;
}

// QEMU virt / RPi3: logical cpu N == Aff0 = N, everything else 0
inline uint64_t mpidr_for_cpu(unsigned cpu_index)
{
    return mpidr_pack(0, 0, 0, cpu_index & 0xFFu);
}

inline void cpu_set_this_id(uint64_t id){
    write_tpidr_el1(id);
}

#endif
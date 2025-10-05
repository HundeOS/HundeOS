// smp_boot.c
#include <stdint.h>
#include "cpu.h"
#include "../kernel/arch/cpu.h"

static uint8_t __attribute__((aligned(16))) cpu_stacks[MAX_CPUS][STACK_SIZE];

inline void wr_vbar_el1(uint64_t v){ __asm__ __volatile__("msr VBAR_EL1, %0"::"r"(v)); __asm__ __volatile__("isb"); }


uint64_t CPU__id()
{
    return cpu_id();
}

void CPU__noop()
{
    __asm__ __volatile__("nop");
}

void CPU__wait() {
    __asm__ __volatile__("wfe");
}

void CPU__yield(void) {
    __asm__ __volatile__("yield");
}

void CPU__wait_for_interrupts() {
    while (1) {
        CPU__wait();
    }
}

void CPU__assign_id(uint64_t id){
    write_tpidr_el1(id);
}

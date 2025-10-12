// smp_boot.c
#include <stdint.h>
#include "cpu.h"
#include "../../kernel/arch/cpu.h"
#include "../../kernel/debug.h"
#include "cpu_common.h"

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
    while (true) {
        CPU__wait();
    }
}

inline void CPU__halt_forever() {
    __asm__ volatile(
        "msr daifset, #0xf\n"   // mask IRQ, FIQ, SError, Debug
        "dsb sy\n"
        "isb\n"
        "1: wfi\n"              // or wfe; we’re not coming back
        "b 1b\n"
        ::: "memory");
}

static _Atomic int halt = 0;

void CPU__halt_all() {
    halt = 1;
    CPU__halt_forever();
}

void CPU__check_halt() {
    if (halt) {
        __d_printf(PANIC, "\n\nCPU %i is halting\n", CPU__id());
        CPU__halt_forever();
    }
}

void CPU__assign_id(uint64_t id){
    write_tpidr_el1(id);
}

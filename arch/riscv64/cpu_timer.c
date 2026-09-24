#include "cpu_timer.h"

#include <stdint.h>

#include "../../kernel/arch/cpu.h"
#include "../../kernel/arch/cpu_timer.h"
#include "../../kernel/cpu_timer.h"
#include "../../kernel/kernel_core.h"

#define SBI_TIME_EXTENSION 0x54494d45ULL
#define SBI_SET_TIMER 0ULL
#define RISCV_SIE_STIE (1ULL << 5U)

static uint64_t timer_interval;

static uint64_t CPUTimer__time(void)
{
    uint64_t ticks;
    __asm__ volatile("rdtime %0" : "=r"(ticks));
    return ticks;
}

static void CPUTimer__set(uint64_t deadline)
{
    register uint64_t arg0 __asm__("a0") = deadline;
    register uint64_t function __asm__("a6") = SBI_SET_TIMER;
    register uint64_t extension __asm__("a7") = SBI_TIME_EXTENSION;
    __asm__ volatile("ecall" : "+r"(arg0) : "r"(function), "r"(extension) : "memory");
}

void CPUTimer__rearm(IRQNumber irq)
{
    CPUTimer__set(CPUTimer__time() + timer_interval);
}

void CPUTimer__start(SystemInfoPtr system_info, uint64_t hz)
{
    timer_interval = CPUTimer__interval(system_info->timer_frequency, hz);

    CPUTimer__rearm(RISCV_INTERRUPT_SUPERVISOR_TIMER);
    __asm__ volatile("csrs sie, %0" : : "r"(RISCV_SIE_STIE) : "memory");
}

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

static uint64_t RiscVCPUTimer__time(void)
{
    uint64_t ticks;
    __asm__ volatile("rdtime %0" : "=r"(ticks));
    return ticks;
}

static void RiscVCPUTimer__set(uint64_t deadline)
{
    register uint64_t arg0 __asm__("a0") = deadline;
    register uint64_t function __asm__("a6") = SBI_SET_TIMER;
    register uint64_t extension __asm__("a7") = SBI_TIME_EXTENSION;
    __asm__ volatile("ecall" : "+r"(arg0) : "r"(function), "r"(extension) : "memory");
}

static void RiscVCPUTimer__rearm(void)
{
    RiscVCPUTimer__set(RiscVCPUTimer__time() + timer_interval);
}

void CPUTimer__start(SystemInfoPtr system_info, uint64_t hz)
{
    uint64_t frequency = system_info->timer_frequency;
    uint64_t target_hz = hz == 0 ? 1000U : hz;
    timer_interval = frequency / target_hz;
    if (timer_interval == 0)
    {
        timer_interval = 1;
    }

    RiscVCPUTimer__rearm();
    __asm__ volatile("csrs sie, %0" : : "r"(RISCV_SIE_STIE) : "memory");
}

void RiscVCPUTimer__handle_interrupt(ProcessContextPtr context, bool current_privilege)
{
    RiscVCPUTimer__rearm();
    if (!current_privilege)
    {
        KernelPtr kernel = Kernel__instance();
        timer_handler(CPU__id() - 1U, kernel->scheduler, context);
    }
}

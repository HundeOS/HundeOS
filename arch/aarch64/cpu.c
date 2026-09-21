// smp_boot.c
#include <stdint.h>
#include "cpu.h"
#include "../../kernel/arch/cpu.h"
#include "../../kernel/debug.h"
#include "cpu_common.h"
#include "ipc/ipc_platform.h"

#define CPU_POKE_SGI 1U

static uint8_t __attribute__((aligned(16))) cpu_stacks[MAX_CPUS][STACK_SIZE];

inline void wr_vbar_el1(uint64_t v)
{
    __asm__ __volatile__("msr VBAR_EL1, %0" ::"r"(v));
    __asm__ __volatile__("isb");
}

uint64_t CPU__id()
{
    return cpu_id();
}

uint64_t CPU__time_usec(void)
{
    return IpcPlatform__monotonic_usec();
}

void CPU__noop()
{
    __asm__ __volatile__("nop");
}

void CPU__wait_for_event()
{
    __asm__ __volatile__("wfe");
}

void CPU__signal_event()
{
    __asm__ __volatile__("sev" ::: "memory");
}

void CPU__wait_for_interrupt()
{
    __asm__ __volatile__("wfi");
}

void CPU__wait()
{
    while (true)
    {
        CPU__wait_for_interrupt();
    }
}

void CPU__yield(void)
{
    __asm__ __volatile__("yield");
}

void CPU__poke(CPUID cpu)
{
    uint64_t target_list = 1ULL << (cpu & 0xf);
    uint64_t sgi = ((uint64_t)CPU_POKE_SGI << 24) | target_list;
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" ::"r"(sgi));
    __asm__ volatile("isb" ::: "memory");
    __d_printf(SCHEDULER, "CPU__poke target=%i sgi=%i\n", (int)cpu, CPU_POKE_SGI);
}

inline void CPU__halt_forever()
{
    __asm__ volatile("msr daifset, #0xf\n" // mask IRQ, FIQ, SError, Debug
                     "dsb sy\n"
                     "isb\n"
                     "1: wfi\n" // or wfe; we’re not coming back
                     "b 1b\n" ::
                         : "memory");
}

static _Atomic int halt = 0;

void CPU__halt_all()
{
    halt = 1;
    CPU__halt_forever();
}

Result(InitCode) CPU__init(SystemInfoPtr system_info)
{
    // Permit read-only EL0 access to the virtual counter for bounded IPC polling.
    uint64_t timer_access;
    __asm__ volatile("mrs %0, cntkctl_el1" : "=r"(timer_access));
    timer_access |= 1ULL << 1;
    __asm__ volatile("msr cntkctl_el1, %0; isb" ::"r"(timer_access) : "memory");
    if (system_info->interrupt_controller.kind != INTERRUPT_CONTROLLER_GIC_V3)
    {
        return Success(InitCode, ARCH_INIT_OK);
    }

    __asm__ __volatile__("mov x0, #0xff            \n"
                         "msr ICC_PMR_EL1, x0      \n" // accept all priorities
                         "mov x0, #1               \n"
                         "msr ICC_IGRPEN1_EL1, x0  \n" // enable Group 1 interrupts
                         "isb                      \n" ::
                             : "memory");

    return Success(InitCode, ARCH_INIT_OK);
}

void CPU__check_halt()
{
    if (halt)
    {
        __d_printf(PANIC, "\n\nCPU %i is halting\n", CPU__id());
        CPU__halt_forever();
    }
}

void CPU__assign_id(uint64_t id)
{
    write_tpidr_el1(id);
}

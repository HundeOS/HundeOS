#include "cpu.h"

#include <stdbool.h>
#include <stdatomic.h>

#include "../../kernel/all_defs.h"
#include "../../kernel/arch/cpu.h"
#include "../../kernel/debug.h"
#include "exception.h"

#define RISCV_COUNTER_TIME (1ULL << 1U)
#define RISCV_SIE_SEIE (1ULL << 9U)
#define RISCV_SSTATUS_SIE (1ULL << 1U)
#define PLIC_SUPERVISOR_CONTEXT_OFFSET 0x201000ULL
#define PLIC_CONTEXT_HART_STRIDE 0x2000ULL

static uint64_t timebase_frequency;
static atomic_bool halt_requested = false;
static atomic_uint_fast64_t physical_hart_ids[MAX_CPUS];

static uint64_t RiscVCPU__time(void)
{
    uint64_t ticks;
    __asm__ volatile("rdtime %0" : "=r"(ticks));
    return ticks;
}

void RiscVCPU__configure_timebase(uint64_t frequency)
{
    timebase_frequency = frequency;
}

CPUID CPU__id(void)
{
    uint64_t hart_id;
    __asm__ volatile("mv %0, tp" : "=r"(hart_id));
    return hart_id + 1U;
}

void RiscVCPU__assign_hart(CPUID logical_id, uint64_t physical_hart_id)
{
    atomic_store_explicit(&physical_hart_ids[logical_id - 1U], physical_hart_id, memory_order_release);
    CPU__assign_id(logical_id);
}

uint64_t RiscVCPU__physical_hart_id(void)
{
    return atomic_load_explicit(&physical_hart_ids[CPU__id() - 1U], memory_order_acquire);
}

uint64_t CPU__time_usec(void)
{
    if (timebase_frequency == 0)
    {
        return 0;
    }

    uint64_t ticks = RiscVCPU__time();
    uint64_t seconds = ticks / timebase_frequency;
    uint64_t remainder = ticks % timebase_frequency;
    return seconds * 1000000ULL + remainder * 1000000ULL / timebase_frequency;
}

void CPU__noop(void)
{
    __asm__ volatile("nop");
}

void CPU__wait_for_event(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

void CPU__signal_event(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

void CPU__wait_for_interrupt(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

void CPU__wait(void)
{
    while (true)
    {
        CPU__wait_for_interrupt();
    }
}

void CPU__yield(void)
{
    __asm__ volatile("nop");
}

static __attribute__((noreturn)) void RiscVCPU__halt_forever(void)
{
    __asm__ volatile("csrci sstatus, 2" ::: "memory");
    while (true)
    {
        __asm__ volatile("wfi");
    }
}

void CPU__halt_all(void)
{
    atomic_store_explicit(&halt_requested, true, memory_order_release);
    RiscVCPU__halt_forever();
}

Result(InitCode) CPU__init(SystemInfoPtr system_info)
{
    RiscVException__install();
    RiscVCPU__configure_timebase(system_info->timer_frequency);
    __asm__ volatile("csrs scounteren, %0" : : "r"(RISCV_COUNTER_TIME) : "memory");
    if (system_info->interrupt_controller.kind == INTERRUPT_CONTROLLER_PLIC)
    {
        ConstPlicInterruptControllerInfoPtr plic = &system_info->interrupt_controller.details.plic;
        Address threshold = plic->virtual_base + PLIC_SUPERVISOR_CONTEXT_OFFSET +
                            RiscVCPU__physical_hart_id() * PLIC_CONTEXT_HART_STRIDE;
        *(volatile uint32_t *)threshold = 0;
        __asm__ volatile("fence iorw, iorw\n"
                         "csrs sie, %0\n"
                         "csrs sstatus, %1"
                         :
                         : "r"(RISCV_SIE_SEIE), "r"(RISCV_SSTATUS_SIE)
                         : "memory");
    }
    return Success(InitCode, ARCH_INIT_OK);
}

void CPU__check_halt(void)
{
    if (atomic_load_explicit(&halt_requested, memory_order_acquire))
    {
        __d_printf(PANIC, "CPU %i is halting\n", CPU__id());
        RiscVCPU__halt_forever();
    }
}

void CPU__assign_id(CPUID id)
{
    uint64_t hart_id = id - 1U;
    __asm__ volatile("mv tp, %0" : : "r"(hart_id));
}

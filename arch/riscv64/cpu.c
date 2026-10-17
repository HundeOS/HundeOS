#include "cpu.h"

#include <stdatomic.h>

#include "../../kernel/all_defs.h"
#include "../../kernel/arch/cpu.h"
#include "exception.h"

#define RISCV_COUNTER_TIME (1ULL << 1U)
#define RISCV_SIE_SEIE (1ULL << 9U)
#define RISCV_SIE_SSIE (1ULL << 1U)
#define RISCV_SSTATUS_SIE (1ULL << 1U)
#define PLIC_SUPERVISOR_ENABLE_OFFSET 0x2080ULL
#define PLIC_ENABLE_HART_STRIDE 0x100ULL
#define PLIC_SUPERVISOR_CONTEXT_OFFSET 0x201000ULL
#define PLIC_CONTEXT_HART_STRIDE 0x2000ULL

static uint64_t timebase_frequency;

static uint64_t RiscVCPU__time(void)
{
    uint64_t ticks;
    __asm__ volatile("rdtime %0" : "=r"(ticks));
    return ticks;
}

static void RiscVCPU__clear_plic_enables(ConstPlicInterruptControllerInfoPtr plic, uint64_t hart_id)
{
    Address enable_address = plic->virtual_base + PLIC_SUPERVISOR_ENABLE_OFFSET + hart_id * PLIC_ENABLE_HART_STRIDE;
    volatile uint32_t * enables = (volatile uint32_t *)enable_address;
    uint64_t enable_word_count = plic->source_count / 32U + 1U;
    for (uint64_t index = 0; index < enable_word_count; index++)
    {
        enables[index] = 0;
    }
}

void RiscVCPU__configure_timebase(uint64_t frequency)
{
    timebase_frequency = frequency;
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

void CPU__request_reschedule(CPUID cpu)
{
    register uint64_t hart_mask __asm__("a0") = 1;
    register uint64_t hart_mask_base __asm__("a1") = cpu;
    register uint64_t function __asm__("a6") = 0;
    register uint64_t extension __asm__("a7") = 0x735049ULL;
    __asm__ volatile("ecall" : "+r"(hart_mask), "+r"(hart_mask_base) : "r"(function), "r"(extension) : "memory");
}

void CPU__wait_for_interrupt(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

void CPU__yield(void)
{
    __asm__ volatile("nop");
}

__attribute__((noreturn)) void CPU__halt_forever(void)
{
    __asm__ volatile("csrci sstatus, 2" ::: "memory");
    while (true)
    {
        __asm__ volatile("wfi");
    }
}

Result(InitCode) CPU__init(SystemInfoPtr system_info)
{
    Exception__install();
    RiscVCPU__configure_timebase(system_info->timer_frequency);
    __asm__ volatile("csrs scounteren, %0" : : "r"(RISCV_COUNTER_TIME) : "memory");
    if (system_info->interrupt_controller.kind == INTERRUPT_CONTROLLER_PLIC)
    {
        ConstPlicInterruptControllerInfoPtr plic = &system_info->interrupt_controller.details.plic;
        uint64_t hart_id = RiscVCPU__physical_hart_id();
        __asm__ volatile("csrc sie, %0" : : "r"(RISCV_SIE_SEIE) : "memory");
        RiscVCPU__clear_plic_enables(plic, hart_id);
        Address threshold = plic->virtual_base + PLIC_SUPERVISOR_CONTEXT_OFFSET + hart_id * PLIC_CONTEXT_HART_STRIDE;
        *(volatile uint32_t *)threshold = 0;
        __asm__ volatile("fence iorw, iorw\n"
                         "csrs sie, %0\n"
                         "csrs sie, %2\n"
                         "csrs sstatus, %1"
                         :
                         : "r"(RISCV_SIE_SEIE), "r"(RISCV_SSTATUS_SIE), "r"(RISCV_SIE_SSIE)
                         : "memory");
    }
    return Success(InitCode, ARCH_INIT_OK);
}

#include "../../kernel/arch/init.h"

#include "../../kernel/boot.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/kernel_init.h"
#include "basic_io.h"
#include "cpu.h"
#include "exception.h"
#include "cpu.h"

#define PLIC_PRIORITY_OFFSET 0ULL
#define PLIC_SUPERVISOR_ENABLE_OFFSET 0x2080ULL
#define PLIC_SUPERVISOR_CONTEXT_OFFSET 0x201000ULL
#define PLIC_CONTEXT_THRESHOLD_OFFSET 0ULL
#define PLIC_ENABLE_HART_STRIDE 0x100ULL
#define PLIC_CONTEXT_HART_STRIDE 0x2000ULL
#define RISCV_SIE_SEIE (1ULL << 9U)
#define RISCV_SSTATUS_SIE (1ULL << 1U)

static BString invalid_interrupt_controller = bstr("RISC-V requires a PLIC interrupt controller");
static BString invalid_interrupt_number = bstr("Invalid PLIC interrupt number");

static volatile uint32_t * RiscVPlic__register(Address base, Address offset)
{
    return (volatile uint32_t *)(base + offset);
}

static bool RiscVPlic__valid_irq(ConstPlicInterruptControllerInfoPtr plic, uint64_t irq)
{
    return irq > 0 && irq <= plic->source_count;
}

static void RiscVPlic__set_enabled(ConstPlicInterruptControllerInfoPtr plic, uint64_t hart_id, uint64_t irq,
                                   bool enabled)
{
    Address enable_offset =
        PLIC_SUPERVISOR_ENABLE_OFFSET + hart_id * PLIC_ENABLE_HART_STRIDE + (irq / 32U) * sizeof(uint32_t);
    volatile uint32_t * enable = RiscVPlic__register(plic->virtual_base, enable_offset);
    uint32_t mask = 1U << (irq % 32U);
    if (enabled)
    {
        *RiscVPlic__register(plic->virtual_base, PLIC_PRIORITY_OFFSET + irq * sizeof(uint32_t)) = 1U;
        *enable |= mask;
    }
    else
    {
        *enable &= ~mask;
    }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

void Boot__init_bootstrap_system(SystemInfoPtr system_info)
{
    KernelPtr kernel = Kernel__instance();
    kernel->system_info = Some(SystemInfo, *system_info);
    kernel->scheduler->interrupt_controller = Some(InterruptControllerInfo, system_info->interrupt_controller);
    kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, system_info->interrupt_controller);
    RiscVCPU__configure_timebase(system_info->timer_frequency);
    RiscVBasicIO__configure(&system_info->uart);
    RiscVException__install();
}

Result(SubsystemInitStatus) Arch__init(KernelPtr kernel)
{
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            if (system_info.interrupt_controller.kind != INTERRUPT_CONTROLLER_PLIC)
            {
                return Error(SubsystemInitStatus, invalid_interrupt_controller, SUBSYSTEM_INIT_FAILED);
            }

            PlicInterruptControllerInfoPtr plic = &system_info.interrupt_controller.details.plic;
            Address context = PLIC_SUPERVISOR_CONTEXT_OFFSET + plic->target_hart_id * PLIC_CONTEXT_HART_STRIDE;
            *RiscVPlic__register(plic->virtual_base, context + PLIC_CONTEXT_THRESHOLD_OFFSET) = 0;
            __asm__ volatile("fence iorw, iorw\n"
                             "csrs sie, %0\n"
                             "csrs sstatus, %1"
                             :
                             : "r"(RISCV_SIE_SEIE), "r"(RISCV_SSTATUS_SIE)
                             : "memory");
            return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
        },
        none()
        {
            return Error(SubsystemInitStatus, invalid_interrupt_controller, SUBSYSTEM_INIT_FAILED);
        }
    );
}

Result(SubsystemInitStatus) Arch__enable_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    if (interrupt_controller->kind != INTERRUPT_CONTROLLER_PLIC)
    {
        return Error(SubsystemInitStatus, invalid_interrupt_controller, SUBSYSTEM_INIT_FAILED);
    }

    PlicInterruptControllerInfoPtr plic = &interrupt_controller->details.plic;
    if (!RiscVPlic__valid_irq(plic, irq))
    {
        return Error(SubsystemInitStatus, invalid_interrupt_number, SUBSYSTEM_INIT_FAILED);
    }

    RiscVPlic__set_enabled(plic, plic->target_hart_id, irq, true);
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__mask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    if (interrupt_controller->kind == INTERRUPT_CONTROLLER_PLIC &&
        RiscVPlic__valid_irq(&interrupt_controller->details.plic, irq))
    {
        PlicInterruptControllerInfoPtr plic = &interrupt_controller->details.plic;
        RiscVPlic__set_enabled(plic, plic->target_hart_id, irq, false);
    }
}

void Arch__unmask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    if (interrupt_controller->kind == INTERRUPT_CONTROLLER_PLIC &&
        RiscVPlic__valid_irq(&interrupt_controller->details.plic, irq))
    {
        PlicInterruptControllerInfoPtr plic = &interrupt_controller->details.plic;
        RiscVPlic__set_enabled(plic, plic->target_hart_id, irq, true);
    }
}

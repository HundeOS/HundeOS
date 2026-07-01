#include "io/uart.h"
#include "../../kernel/arch/init.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/kernel_init.h"
#include "../../kernel/irq/irq_dispatcher.h"

#include "asm_helpers.h"
#include "cpu_timer.h"
#include "io/gicd.h"

Result(SubsystemInitStatus) Arch__init()
{
    ResultMatch(InitCode, uart_enable_irqs())
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End
    ResultMatch(InitCode, gic_init())
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End

    KernelPtr kernel = Kernel__instance();
    ResultMatch(IRQRegistrationStatus, IRQDispatcher__register_kernel(
        kernel->irq_dispatcher, PPI_CNTV, CPUTimer__handle_irq, NULL
    ))
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End
    ResultMatch(IRQRegistrationStatus, IRQDispatcher__register_kernel(
        kernel->irq_dispatcher, PPI_CNTP, CPUTimer__handle_irq, NULL
    ))
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End

    enable_irq();

    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

Result(SubsystemInitStatus) Arch__enable_irq(uint64_t irq)
{
    __d_printf(IODEV, "arch enable irq=%i\n", (int) irq);
    ResultMatch(InitCode, gic_enable_interrupt((uint32_t) irq))
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__mask_irq(uint64_t irq)
{
    __d_printf(IODEV, "arch mask irq=%i\n", (int) irq);
    gic_mask_interrupt((uint32_t) irq);
}

void Arch__unmask_irq(uint64_t irq)
{
    __d_printf(IODEV, "arch unmask irq=%i\n", (int) irq);
    gic_unmask_interrupt((uint32_t) irq);
}

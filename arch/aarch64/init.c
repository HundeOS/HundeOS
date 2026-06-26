#include "io/uart.h"
#include "../../kernel/arch/init.h"

#include "asm_helpers.h"
#include "io/gicd.h"

Result(SubsystemInitStatus) Arch__init()
{
    ResultMatch(InitCode, uart_enable_irqs())
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End
    ResultMatch(InitCode, gic_init())
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End
    ResultMatch(InitCode, gic_enable_interrupt(UART_IRQ_ID))
        IsError(code, message, return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED));
    End

    enable_irq();

    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__mask_irq(uint64_t irq)
{
    gic_mask_interrupt((uint32_t) irq);
}

void Arch__unmask_irq(uint64_t irq)
{
    gic_unmask_interrupt((uint32_t) irq);
}

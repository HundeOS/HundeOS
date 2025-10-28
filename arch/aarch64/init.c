#include "io/uart.h"
#include "../../kernel/arch/init.h"

#include "asm_helpers.h"
#include "io/gicd.h"

Result(InitCode) Arch__init()
{
    // ResultMatch(InitCode, uart_init())
    //     IsError(code, message, return Error(InitCode, message, code));
    // End
    ResultMatch(InitCode, uart_enable_irqs())
        IsError(code, message, return Error(InitCode, message, code));
    End
    ResultMatch(InitCode, gic_init())
        IsError(code, message, return Error(InitCode, message, code));
    End
    ResultMatch(InitCode, gic_enable_interrupt(UART_IRQ_ID))
        IsError(code, message, return Error(InitCode, message, code));
    End

    enable_irq();

    return Success(InitCode, ARCH_INIT_OK);
}
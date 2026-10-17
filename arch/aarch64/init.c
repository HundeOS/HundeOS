#include "io/uart.h"
#include "../../kernel/arch/init.h"
#include "../../kernel/cpu_timer.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/kernel_init.h"
#include "../../kernel/irq/irq_dispatcher.h"

#include "asm_helpers.h"
#include "cpu_timer.h"
#include "io/gicd.h"

void Arch__init_bootstrap_system(SystemInfoPtr system_info)
{
    uart_use_info(&system_info->trace_uart);
}

Result(SubsystemInitStatus) Arch__init(KernelPtr kernel)
{
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            match_result(
                gic_init(&system_info.interrupt_controller),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
                }
            );

            match_result(
                IRQDispatcher__register_kernel(kernel->irq_dispatcher, PPI_CNTV, CPUTimer__handle_irq, kernel),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
                }
            );
            match_result(
                IRQDispatcher__register_kernel(kernel->irq_dispatcher, PPI_CNTP, CPUTimer__handle_irq, kernel),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
                }
            );

            enable_irq();

            return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
        },
        none()
        {
            return Error(SubsystemInitStatus, bstr("Missing system info"), SUBSYSTEM_INIT_FAILED);
        }
    );
}

Result(SubsystemInitStatus) Arch__enable_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    __d_printf(IRQ, "arch enable irq=%i\n", (int)irq);
    match_result(
        gic_enable_interrupt(interrupt_controller, (uint32_t)irq),
        success_do_nothing(),
        error(code, message)
        {
            return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
        }
    );
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__mask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    __d_printf(IRQ, "arch mask irq=%i\n", (int)irq);
    gic_mask_interrupt(interrupt_controller, (uint32_t)irq);
}

void Arch__unmask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    __d_printf(IRQ, "arch unmask irq=%i\n", (int)irq);
    gic_unmask_interrupt(interrupt_controller, (uint32_t)irq);
}

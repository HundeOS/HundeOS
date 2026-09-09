#include "io/uart.h"
#include "../../kernel/boot.h"
#include "../../kernel/arch/init.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/kernel_init.h"
#include "../../kernel/irq/irq_dispatcher.h"

#include "asm_helpers.h"
#include "cpu_timer.h"
#include "io/gicd.h"

void Boot__init_bootstrap_system(SystemInfoPtr system_info)
{
    KernelPtr kernel = Kernel__instance();
    kernel->system_info = Some(SystemInfo, *system_info);
    SystemInfoPtr installed_system_info = &kernel->system_info.some.val;
    kernel->scheduler->interrupt_controller =
        Some(InterruptControllerInfo, installed_system_info->interrupt_controller);
    kernel->irq_dispatcher->interrupt_controller =
        Some(InterruptControllerInfo, installed_system_info->interrupt_controller);
    uart_use_info(&installed_system_info->uart);
}

Result(SubsystemInitStatus) Arch__init(KernelPtr kernel)
{
    SystemInfoPtr system_info = &kernel->system_info.some.val;

    match_result(
        gic_init(&system_info->interrupt_controller),
        success_do_nothing(),
        error(code, message) { return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED); }
    );

    match_result(
        IRQDispatcher__register_kernel(kernel->irq_dispatcher, PPI_CNTV, CPUTimer__handle_irq, NULL),
        success_do_nothing(),
        error(code, message) { return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED); }
    );
    match_result(
        IRQDispatcher__register_kernel(kernel->irq_dispatcher, PPI_CNTP, CPUTimer__handle_irq, NULL),
        success_do_nothing(),
        error(code, message) { return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED); }
    );

    enable_irq();

    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

Result(SubsystemInitStatus) Arch__enable_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    __d_printf(IODEV, "arch enable irq=%i\n", (int)irq);
    match_result(
        gic_enable_interrupt(interrupt_controller, (uint32_t)irq),
        success_do_nothing(),
        error(code, message) { return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED); }
    );
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Arch__mask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    __d_printf(IODEV, "arch mask irq=%i\n", (int)irq);
    gic_mask_interrupt(interrupt_controller, (uint32_t)irq);
}

void Arch__unmask_irq(InterruptControllerInfoPtr interrupt_controller, uint64_t irq)
{
    __d_printf(IODEV, "arch unmask irq=%i\n", (int)irq);
    gic_unmask_interrupt(interrupt_controller, (uint32_t)irq);
}

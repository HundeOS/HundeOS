#include <stdint.h>

#include "../../kernel/kernel.h"
#include "cpu_timer.h"
#include "exception.h"
#include "cpu.h"

#include <stdint.h>

#include "asm_helpers.h"
#include "exception_decode.h"
#include "io/gicd.h"
#include "process.h"
#include "../../kernel/debug.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/irq/irq_dispatcher.h"
#include "../../kernel/syscall/syscall_dispatcher.h"
#include "../../kernel/types/optional.h"
#include "ipc/ipc_platform.h"

static inline ExceptionSyndrome Exception__syndrome(void)
{
    ExceptionSyndrome syndrome;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(syndrome));
    return syndrome;
}

static inline Address Exception__fault_address(void)
{
    Address far;
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    return far;
}

static inline Address Exception__return_address(void)
{
    Address elr;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    return elr;
}

static void Exception__account_current_process(KernelPtr kernel, CPUUsageMode mode)
{
    match_optional(
        Scheduler__current_process(kernel->scheduler, CPU__id()),
        some(process)
        {
            Process__account_cpu_mode(process, mode);
        },
        none_do_nothing()
    );
}

static void Exception__prepare_user_return(KernelPtr kernel, ProcessContextPtr context)
{
    match_optional(
        Scheduler__current_process(kernel->scheduler, CPU__id()),
        some(process)
        {
            if (Scheduler__deliver_pending_signal(kernel->scheduler, process, context) == PROCESS_SIGNAL_DELIVERY_STOP)
            {
                Scheduler__yield_waiting(kernel->scheduler, process, CPU__id());
            }
        },
        none_do_nothing()
    );
}

static void Exception__handle_irq_with_controller(ProcessContextPtr context, bool current_el,
                                                  InterruptControllerInfoPtr interrupt_controller)
{
    KernelPtr kernel = Kernel__instance();
    if (!current_el)
    {
        Exception__account_current_process(kernel, CPU_USAGE_PAUSED);
    }

    GicInterrupt interrupt = gic_acknowledge_interrupt(interrupt_controller);
    uint32_t id = interrupt.irq;
    __d_printf(IRQ, "irq: %i\n", id);
    if (id >= 32)
    {
        __d_printf(IRQ, "irq entry id=%i cpu=%i current_el=%i\n", id, CPU__id(), current_el);
    }

    __d_printf(IRQ, "ELR: %p, SP_ELO: %p\n", Exception__return_address(), read_sp_el0());

    CPUID cpu = CPU__id();
    if (id != PPI_CNTV && id != PPI_CNTP)
    {
        Optional(ProcessPtr) interrupted_process =
            Scheduler__save_context(kernel->scheduler, cpu, &Some(ProcessContextPtr, context));
        match_optional(
            interrupted_process,
            some(proc)
            {
                __d_printf(IRQ, "IRQ saved context pid=%i name=%s\n", proc->pid, cstr(proc->name));
                ;
            },
            none()
            {
                __d_println(IRQ, "IRQ no process context saved");
                ;
            }
        );
    }

    gic_complete_interrupt(interrupt_controller, &interrupt);

    if (id == CPU_RESCHEDULE_INTERRUPT)
    {
        IpcPlatform__signal_event();
        if (!current_el)
        {
            Scheduler__preempt(kernel->scheduler, cpu, &Some(ProcessContextPtr, context));
            Exception__account_current_process(kernel, CPU_USAGE_USER);
            Exception__prepare_user_return(kernel, context);
        }
        return;
    }

    IRQDispatchContext dispatch_context = IRQDispatchContext__new(id, cpu, current_el, context);

    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, id, &dispatch_context),
        success(status)
        {
            if (id != PPI_CNTV && id != PPI_CNTP)
            {
                IpcPlatform__signal_event();
            }
            if (id >= 32)
            {
                __d_printf(IRQ, "irq dispatch id=%i status=%i\n", id, status);
            }
            if (status == IRQ_DISPATCH_UNHANDLED)
            {
                __d_printf(UNHANDLED, "unhandled irq: %i\n", id);
            }
            if (!current_el && status == IRQ_DISPATCH_RESCHEDULE)
            {
                __d_printf(IRQ, "irq dispatch id=%i requested reschedule cpu=%i\n", id, cpu);
                Scheduler__preempt(kernel->scheduler, cpu, &Some(ProcessContextPtr, context));
            }
        },
        error(code, message)
        {
            __d_printf(UNHANDLED, "irq dispatch error %i for irq %i\n", code, id);
        }
    );
    if (!current_el)
    {
        Exception__account_current_process(kernel, CPU_USAGE_USER);
        Exception__prepare_user_return(kernel, context);
    }
}

static void Exception__handle_irq(ProcessContextPtr context, bool current_el)
{
    KernelPtr kernel = Kernel__instance();
    match_optional(
        kernel->irq_dispatcher->interrupt_controller,
        some(interrupt_controller)
        {
            Exception__handle_irq_with_controller(context, current_el, &interrupt_controller);
        },
        none()
        {
            Kernel__panic("AArch64 interrupt without interrupt controller information", context);
        }
    );
}

static uint64_t Exception__abort_user(KernelPtr kernel, OptionalPtr(ProcessPtr) current_process,
                                      ProcessContextPtr context, ProcessAbortReason reason, ExceptionSyndrome syndrome,
                                      uint64_t ec)
{
    match_optional(
        *current_process,
        some(process)
        {
            ProcessAbort abort;
            abort.reason = reason;
            abort.exception_description = Exception__class_name(ec);
            abort.fault_status_description =
                Exception__has_fault_status(ec) ? Exception__fault_status_name(syndrome) : "not applicable";
            abort.access_description =
                reason == PROCESS_ABORT_DATA_ABORT ? Exception__data_abort_access_name(syndrome) : "not applicable";
            abort.pc = context->elr;
            abort.fault_address = Exception__fault_address();
            abort.exception_code = ec;
            abort.fault_status_code = Exception__fault_status_code(syndrome);
            abort.syndrome = syndrome;
            abort.cpu = CPU__id();

            match_result(
                Scheduler__abort_process(kernel->scheduler, process, abort),
                success_do_nothing(),
                error(code, message)
                {
                    Kernel__panic(cstr(message), context);
                }
            );
        },
        none()
        {
            Kernel__panic("User exception without a current process", context);
        }
    );

    return 0;
}

static bool Exception__yield_if_parked(KernelPtr kernel, OptionalPtr(ProcessPtr) current_process)
{
    match_optional(
        *current_process,
        some(process)
        {
            if (Process__is_parked(process))
            {
                __d_printf(SYSCALL, "syscall parked pid=%i name=%s yielding\n", process->pid, cstr(process->name));
                Scheduler__yield_waiting(kernel->scheduler, process, CPU__id());
                return true;
            }
        },
        none_do_nothing()
    );

    return false;
}

static uint64_t Exception__handle_sync(ProcessContextPtr context, bool current_el)
{
    ExceptionSyndrome syndrome = Exception__syndrome();
    uint64_t ec = (syndrome >> 26) & 0x3F; // Exception class
    Address elr = Exception__return_address();
    Address far = Exception__fault_address();

    __d_printf(SYSCALL,
               "sync call: ec=0x%x syndrome=%p elr=%p far=%p context_elr=%p x8=%p x0=%p current_el=%i cpu=%i\n", ec,
               syndrome, elr, far, context->elr, context->x[8], context->x[0], current_el, CPU__id());

    KernelPtr kernel = Kernel__instance();
    Optional(ProcessPtr) current_process =
        Scheduler__save_context(kernel->scheduler, CPU__id(), &Some(ProcessContextPtr, context));

    // Handle synchronous exceptions (e.g., undefined instruction, SVC, data abort)
    switch (ec)
    {
    case 0x15: // SVC instruction execution
        match_optional(
            current_process,
            some(proc)
            {
                Process__account_cpu_mode(proc, CPU_USAGE_SYSTEM);
                __d_printf(SYSCALL, "syscall entry pid=%i name=%s id=%p elr=%p args=%p,%p,%p,%p,%p current_el=%i\n",
                           proc->pid, cstr(proc->name), context->x[8], context->elr, context->x[0], context->x[1],
                           context->x[2], context->x[3], context->x[4], current_el);
            },
            none()
            {
                __d_printf(SYSCALL, "syscall entry no-current-process id=%p elr=%p current_el=%i\n", context->x[8],
                           context->elr, current_el);
            }
        );
        __d_printf(SYSCALL, "system call: %p\n", context->x[8]);
        __d_printf(SYSCALL, "system call args: %p %p %p %p %p\n", context->x[0], context->x[1], context->x[2],
                   context->x[3], context->x[4]);

        Result(SystemCallResult) syscall_result =
            SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &current_process, context->x[8],
                                        context->x[0], context->x[1], context->x[2], context->x[3], context->x[4]);

        match_result(
            syscall_result,
            success(call_result)
            {
                match_optional(
                    current_process,
                    some(proc)
                    {
                        __d_printf(SYSCALL, "syscall exit pid=%i name=%s id=%p result=%p\n", proc->pid,
                                   cstr(proc->name), context->x[8], call_result);
                        ;
                    },
                    none()
                    {
                        __d_printf(SYSCALL, "syscall exit no-current-process id=%p result=%p\n", context->x[8],
                                   call_result);
                        ;
                    }
                );
                __d_printf(SYSCALL, "syscall result: res=%p\n", call_result);
                if (Exception__yield_if_parked(kernel, &current_process))
                {
                    return 0;
                }
                if (SystemCallResult__restores_context(context->x[8], call_result))
                {
                    match_optional(
                        current_process,
                        some(proc)
                        {
                            ProcessContext__copy(context, Process__required_context(proc));
                        },
                        none_do_nothing()
                    );
                }
                else
                {
                    context->x[0] = call_result;
                }
                Exception__account_current_process(kernel, CPU_USAGE_USER);
                Exception__prepare_user_return(kernel, context);
                return context->x[0];
            },
            error(code, message)
            {
                if (Exception__yield_if_parked(kernel, &current_process))
                {
                    return 0;
                }
                context->x[0] = code;
                Exception__account_current_process(kernel, CPU_USAGE_USER);
                match_optional(
                    current_process,
                    some(proc)
                    {
                        __d_printf(SYSCALL, "syscall error pid=%i name=%s id=%p code=%i message=%s\n", proc->pid,
                                   cstr(proc->name), context->x[8], code, BString__cstr(&message));
                        ;
                    },
                    none()
                    {
                        __d_printf(SYSCALL, "syscall error no-current-process id=%p code=%i message=%s\n",
                                   context->x[8], code, BString__cstr(&message));
                        ;
                    }
                );
                __d_printf(SYSCALL, "syscall error: %s code=%i\n", BString__cstr(&message), code);
                Exception__prepare_user_return(kernel, context);
                return context->x[0];
            }
        );
        return 0;
        break;
    case 0x20: // Instruction abort from lower EL
        if (current_el)
        {
            Kernel__panic("Instruction abort from current EL", context);
        }
        return Exception__abort_user(kernel, &current_process, context, PROCESS_ABORT_INSTRUCTION_ABORT, syndrome, ec);
        break;
    case 0x21: // Instruction abort from current EL
        Kernel__panic("Instruction abort from current EL", context);
        return 0;
        break;
    case 0x24: // Data abort from lower EL
        if (current_el)
        {
            Kernel__panic("Data abort from current EL", context);
        }
        return Exception__abort_user(kernel, &current_process, context, PROCESS_ABORT_DATA_ABORT, syndrome, ec);
        break;
    case 0x25: // Data abort from current EL
        Kernel__panic("Data abort from current EL", context);
        return 0;
        break;
    default:
        // Handle other synchronous exceptions
        Kernel__panic("Unhandled CPU exception", context);
        return 0;
        break;
    }
}

// Current EL with SP0 handlers
uint64_t Exception__handle_sync_current_sp0(ProcessContextPtr context)
{
    __d_println(IRQ, "\nException__handle_sync_current_sp0");
    return Exception__handle_sync(context, true);
}

void Exception__handle_irq_current_sp0(ProcessContextPtr context)
{
    // Handle IRQ interrupt
    // Process pending interrupts from interrupt controller
    __d_println(IRQ, "\nException__handle_irq_current_sp0");
    Exception__handle_irq(context, true);
}

void Exception__handle_fiq_current_sp0(ProcessContextPtr context)
{
    // Handle FIQ (fast interrupt)
    __d_println(IRQ, "\nException__handle_fiq_current_sp0");
}

void Exception__handle_serror_current_sp0(ProcessContextPtr context)
{
    // Handle system error (SError)
    // This is typically fatal
    __d_println(PANIC, "\nException__handle_serror_current_sp0");
}

// Current EL with SPx handlers
uint64_t Exception__handle_sync_current_spx(ProcessContextPtr context)
{
    __d_println(IRQ, "\nException__handle_sync_current_spx");
    return Exception__handle_sync(context, true);
    // Handle synchronous exceptions from current EL
}

void Exception__handle_irq_current_spx(ProcessContextPtr context)
{
    // Handle IRQ from current EL with SPx
    __d_println(IRQ, "\nException__handle_irq_current_spx");
    Exception__handle_irq(context, true);
}

void Exception__handle_fiq_current_spx(ProcessContextPtr context)
{
    // Handle FIQ from current EL with SPx
    __d_println(IRQ, "\nException__handle_fiq_current_spx");
}

void Exception__handle_serror_current_spx(ProcessContextPtr context)
{
    // Handle SError from current EL with SPx
    __d_println(PANIC, "\nException__handle_serror_current_spx");
}

// Lower EL using AArch64 handlers
uint64_t Exception__handle_sync_lower_aarch64(ProcessContextPtr context)
{
    __d_println(IRQ, "\nException__handle_sync_lower_aarch64");
    return Exception__handle_sync(context, false);
}

void Exception__handle_irq_lower_aarch64(ProcessContextPtr context)
{
    // Handle IRQ from lower EL (AArch64)
    __d_println(IRQ, "\nException__handle_irq_lower_aarch64");
    Exception__handle_irq(context, false);
}

void Exception__handle_fiq_lower_aarch64(ProcessContextPtr context)
{
    // Handle FIQ from lower EL (AArch64)
    __d_println(IRQ, "\nException__handle_fiq_lower_aarch64");
}

void Exception__handle_serror_lower_aarch64(ProcessContextPtr context)
{
    // Handle SError from lower EL (AArch64)
    __d_println(PANIC, "\nException__handle_serror_lower_aarch64");
}

// Lower EL using AArch32 handlers
void Exception__handle_sync_lower_aarch32(ProcessContextPtr context)
{
    // Handle synchronous exceptions from AArch32 lower EL
    __d_println(IRQ, "\nException__handle_sync_lower_aarch32");
}

void Exception__handle_irq_lower_aarch32(ProcessContextPtr context)
{
    // Handle IRQ from lower EL (AArch32)
    __d_println(IRQ, "\nException__handle_irq_lower_aarch32");
    Exception__handle_irq(context, false);
}

void Exception__handle_fiq_lower_aarch32(ProcessContextPtr context)
{
    // Handle FIQ from lower EL (AArch32)
    __d_println(IRQ, "\nException__handle_fiq_lower_aarch32");
}

void Exception__handle_serror_lower_aarch32(ProcessContextPtr context)
{
    // Handle SError from lower EL (AArch32)
    __d_println(PANIC, "\nException__handle_serror_lower_aarch32");
}

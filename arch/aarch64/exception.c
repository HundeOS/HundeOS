#include <stdint.h>

#include "../../kernel/kernel.h"
#include "cpu_timer.h"
#include "exception.h"

#include <stdint.h>

#include "asm_helpers.h"
#include "exception_decode.h"
#include "../../kernel/debug.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/irq/irq_dispatcher.h"
#include "../../kernel/syscall/syscall_dispatcher.h"
#include "../../kernel/types/optional.h"

#define CPU_POKE_SGI 1U

// Helper function to get exception syndrome
inline uint64_t get_esr_el1(void) {
    uint64_t esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r" (esr));
    return esr;
}

inline uint64_t get_far_el1(void) {
    uint64_t far;
    __asm__ volatile("mrs %0, far_el1" : "=r" (far));
    return far;
}

inline uint64_t get_elr_el1(void) {
    uint64_t elr;
    __asm__ volatile("mrs %0, elr_el1" : "=r" (elr));
    return elr;
}

void irq_handler(ProcessContextPtr context, bool current_el)
{
    KernelPtr kernel = Kernel__instance();

    uint32_t id = icc_iar1();
    __d_printf(IRQ, "irq: %i\n", id);
    if (id >= 32)
    {
        __d_printf(IODEV, "irq entry id=%i cpu=%i current_el=%i\n", id, CPU__id() - 1, current_el);
    }

    __d_printf(IRQ, "ELR: %p, SP_ELO: %p\n", get_elr_el1(), read_sp_el0());

    CPUID cpu = CPU__id() - 1;
    if (id != PPI_CNTV && id != PPI_CNTP)
    {
        Optional(ProcessPtr) interrupted_process = Scheduler__save_context(kernel->scheduler, cpu, context);
        Match(ProcessPtr, interrupted_process)
            IsSome(proc, __d_printf(IRQ, "IRQ saved context pid=%i name=%s\n", proc->pid, cstr(proc->name));)
            IsNone(__d_println(IRQ, "IRQ no process context saved");)
        End
    }

    icc_eoir1(id);
    icc_dir(id);

    if (id == CPU_POKE_SGI)
    {
        __d_printf(SCHEDULER, "cpu poke irq cpu=%i current_el=%i\n", cpu, current_el);
        if (!current_el)
        {
            Scheduler__preempt(kernel->scheduler, cpu, context);
        }
        return;
    }

    IRQDispatchContext dispatch_context = (IRQDispatchContext) {
        .irq = id,
        .cpu = cpu,
        .current_el = current_el,
        .process_context = context,
    };

    ResultMatch(IRQDispatchStatus, IRQDispatcher__dispatch(kernel->irq_dispatcher, id, &dispatch_context))
        IsSuccess(status,
        {
            if (id >= 32)
            {
                __d_printf(IODEV, "irq dispatch id=%i status=%i\n", id, status);
            }
            if (status == IRQ_DISPATCH_UNHANDLED)
            {
                __d_printf(UNHANDLED, "unhandled irq: %i\n", id);
            }
            if (!current_el && status == IRQ_DISPATCH_RESCHEDULE)
            {
                __d_printf(IODEV, "irq dispatch id=%i requested reschedule cpu=%i\n", id, cpu);
                Scheduler__preempt(kernel->scheduler, cpu, context);
            }
        })
        IsError(code, message, __d_printf(UNHANDLED, "irq dispatch error %i for irq %i\n", code, id))
    End
}

static uint64_t abort_user_process(
    KernelPtr kernel,
    Optional(ProcessPtr) current_process,
    ProcessContextPtr context,
    ProcessAbortReason reason,
    uint64_t esr,
    uint64_t ec
)
{
    Match(ProcessPtr, current_process)
        IsSome(process,
        {
            ProcessAbort abort;
            abort.reason = reason;
            abort.exception_description = AArch64Exception__class_name(ec);
            abort.fault_status_description = AArch64Exception__has_fault_status(ec)
                ? AArch64Exception__fault_status_name(esr)
                : "not applicable";
            abort.access_description = reason == PROCESS_ABORT_DATA_ABORT
                ? AArch64Exception__data_abort_access_name(esr)
                : "not applicable";
            abort.pc = context->elr;
            abort.fault_address = get_far_el1();
            abort.exception_code = ec;
            abort.fault_status_code = AArch64Exception__fault_status_code(esr);
            abort.syndrome = esr;
            abort.cpu = CPU__id();

            ResultMatch(ProcessPtr, Scheduler__abort_process(kernel->scheduler, kernel->io_subsystem, process, abort))
                IsError(code, message, Kernel__panic(cstr(message), context))
            End
        })
        IsNone(Kernel__panic("User exception without a current process", context))
    End

    return 0;
}

static bool yield_if_process_parked(KernelPtr kernel, Optional(ProcessPtr) current_process)
{
    Match(ProcessPtr, current_process)
        IsSome(process,
        {
            if (Process__is_parked(process))
            {
                __d_printf(
                    SYSCALL,
                    "syscall parked pid=%i name=%s yielding\n",
                    process->pid,
                    cstr(process->name)
                );
                Scheduler__yield_waiting(kernel->scheduler, process, CPU__id() - 1);
                return true;
            }
        })
    End

    return false;
}

uint64_t handle_sync(ProcessContextPtr context, bool current_el) {
    uint64_t esr = get_esr_el1();
    uint64_t ec = (esr >> 26) & 0x3F;  // Exception class

    __d_printf(SYSCALL, "sync call: 0x%x\n", ec);

    KernelPtr kernel = Kernel__instance();
    Optional(ProcessPtr) current_process = Scheduler__save_context(kernel->scheduler, CPU__id() - 1, context);

    // Handle synchronous exceptions (e.g., undefined instruction, SVC, data abort)
    switch(ec) {
        case 0x15: // SVC instruction execution
            Match(ProcessPtr, current_process)
                IsSome(proc,
                {
                    __d_printf(
                        SYSCALL,
                        "syscall entry pid=%i name=%s id=%p elr=%p args=%p,%p,%p,%p,%p current_el=%i\n",
                        proc->pid,
                        cstr(proc->name),
                        context->x[8],
                        context->elr,
                        context->x[0],
                        context->x[1],
                        context->x[2],
                        context->x[3],
                        context->x[4],
                        current_el
                    );
                })
                IsNone(
                {
                    __d_printf(
                        SYSCALL,
                        "syscall entry no-current-process id=%p elr=%p current_el=%i\n",
                        context->x[8],
                        context->elr,
                        current_el
                    );
                })
            End
            __d_printf(SYSCALL, "system call: %p\n", context->x[8]);
            __d_printf(SYSCALL, "system call args: %p %p %p %p %p\n",
                context->x[0], context->x[1], context->x[2], context->x[3], context->x[4]);


            Result(SystemCallResult) syscall_result =
                SyscallDispatcher__dispatch(
                    &kernel->syscall_dispatcher,
                    kernel,
                    current_process,
                    context->x[8],
                    context->x[0],
                    context->x[1],
                    context->x[2],
                    context->x[3],
                    context->x[4]);

            ResultMatch(SystemCallResult, syscall_result);
                IsSuccess(call_result,
                {
                    Match(ProcessPtr, current_process)
                        IsSome(proc, __d_printf(
                            SYSCALL,
                            "syscall exit pid=%i name=%s id=%p result=%p\n",
                            proc->pid,
                            cstr(proc->name),
                            context->x[8],
                            call_result
                        );)
                        IsNone(__d_printf(SYSCALL, "syscall exit no-current-process id=%p result=%p\n", context->x[8], call_result);)
                    End
                    __d_printf(SYSCALL, "syscall result: res=%p\n", call_result);
                    if (yield_if_process_parked(kernel, current_process))
                    {
                        return 0;
                    }
                    return call_result;
                });
                IsError(code, message,
                {
                    if (yield_if_process_parked(kernel, current_process))
                    {
                        return 0;
                    }
                    Match(ProcessPtr, current_process)
                        IsSome(proc, __d_printf(
                            SYSCALL,
                            "syscall error pid=%i name=%s id=%p code=%i message=%s\n",
                            proc->pid,
                            cstr(proc->name),
                            context->x[8],
                            code,
                            KString__cstr(&message)
                        );)
                        IsNone(__d_printf(
                            SYSCALL,
                            "syscall error no-current-process id=%p code=%i message=%s\n",
                            context->x[8],
                            code,
                            KString__cstr(&message)
                        );)
                    End
                    __d_printf(SYSCALL, "syscall error: %s code=%i\n", KString__cstr(&message), code);
                    return code;
                });
            End
            return 0;
            break;
        case 0x20: // Instruction abort from lower EL
            if (current_el)
            {
                Kernel__panic("Instruction abort from current EL", context);
            }
            return abort_user_process(
                kernel, current_process, context, PROCESS_ABORT_INSTRUCTION_ABORT, esr, ec
            );
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
            return abort_user_process(
                kernel, current_process, context, PROCESS_ABORT_DATA_ABORT, esr, ec
            );
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
uint64_t c_sync_handler_current_el_sp0(ProcessContextPtr context) {
    __d_println(IRQ, "\nc_sync_handler_current_el_sp0");
    return handle_sync(context, true);
}

void c_irq_handler_current_el_sp0(ProcessContextPtr context) {
    // Handle IRQ interrupt
    // Process pending interrupts from interrupt controller
    __d_println(IRQ, "\nc_irq_handler_current_el_sp0");
    irq_handler(context, true);
}

void c_fiq_handler_current_el_sp0(ProcessContextPtr context) {
    // Handle FIQ (fast interrupt)
    __d_println(IRQ, "\nc_fiq_handler_current_el_sp0");
}

void c_serror_handler_current_el_sp0(ProcessContextPtr context) {
    // Handle system error (SError)
    // This is typically fatal
    __d_println(PANIC, "\nc_serror_handler_current_el_sp0");
}

// Current EL with SPx handlers
uint64_t c_sync_handler_current_el_spx(ProcessContextPtr context) {
    __d_println(IRQ, "\nc_sync_handler_current_el_spx");
    return handle_sync(context, true);
    // Handle synchronous exceptions from current EL
}

void c_irq_handler_current_el_spx(ProcessContextPtr context) {
    // Handle IRQ from current EL with SPx
    __d_println(IRQ, "\nc_irq_handler_current_el_spx");
    irq_handler(context, true);
}

void c_fiq_handler_current_el_spx(ProcessContextPtr context) {
    // Handle FIQ from current EL with SPx
    __d_println(IRQ, "\nc_fiq_handler_current_el_spx");
}

void c_serror_handler_current_el_spx(ProcessContextPtr context) {
    // Handle SError from current EL with SPx
    __d_println(PANIC, "\nc_serror_handler_current_el_spx");
}

// Lower EL using AArch64 handlers
void c_sync_handler_lower_el_aarch64(ProcessContextPtr context) {
    __d_println(IRQ, "\nc_sync_handler_lower_el_aarch64");
    handle_sync(context, false);
}

void c_irq_handler_lower_el_aarch64(ProcessContextPtr context) {
    // Handle IRQ from lower EL (AArch64)
    __d_println(IRQ, "\nc_irq_handler_current_el_aarch64");
    irq_handler(context, false);
}

void c_fiq_handler_lower_el_aarch64(ProcessContextPtr context) {
    // Handle FIQ from lower EL (AArch64)
    __d_println(IRQ, "\nc_fiq_handler_current_el_aarch64");
}

void c_serror_handler_lower_el_aarch64(ProcessContextPtr context) {
    // Handle SError from lower EL (AArch64)
    __d_println(PANIC, "\nc_serror_handler_current_el_aarch64");
}

// Lower EL using AArch32 handlers
void c_sync_handler_lower_el_aarch32(ProcessContextPtr context) {
    // Handle synchronous exceptions from AArch32 lower EL
    __d_println(IRQ, "\nc_sync_handler_lower_el_aarch32");
}

void c_irq_handler_lower_el_aarch32(ProcessContextPtr context) {
    // Handle IRQ from lower EL (AArch32)
    __d_println(IRQ, "\nc_irq_handler_lower_el_aarch32");
    irq_handler(context, false);
}

void c_fiq_handler_lower_el_aarch32(ProcessContextPtr context) {
    // Handle FIQ from lower EL (AArch32)
    __d_println(IRQ, "\nc_fiq_handler_lower_el_aarch32");
}

void c_serror_handler_lower_el_aarch32(ProcessContextPtr context) {
    // Handle SError from lower EL (AArch32)
    __d_println(PANIC, "\nc_serror_handler_lower_el_aarch32");
}

// Initialize vector table
void init_vector_table(void) {
    extern void *vector_table;
    uint64_t vbar = (uint64_t)&vector_table;
    
    // Set VBAR_EL1 (or appropriate EL)
    __asm__ volatile("msr vbar_el1, %0" : : "r" (vbar));
    __asm__ volatile("isb");
}

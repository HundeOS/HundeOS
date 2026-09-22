#include "exception.h"

#include <stdbool.h>
#include <stdint.h>

#include "../../kernel/arch/cpu.h"
#include "../../kernel/debug.h"
#include "../../kernel/irq/irq_dispatcher.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/panic.h"
#include "../../kernel/scheduler/scheduler.h"
#include "../../kernel/syscall/syscall_dispatcher.h"
#include "cpu_timer.h"
#include "cpu.h"
#include "ipc/ipc_platform.h"

#define RISCV_SSTATUS_SPP (1ULL << 8U)
#define RISCV_INTERRUPT_FLAG (1ULL << 63U)
#define RISCV_INTERRUPT_SUPERVISOR_TIMER 5ULL
#define RISCV_INTERRUPT_SUPERVISOR_EXTERNAL 9ULL
#define RISCV_EXCEPTION_USER_ECALL 8ULL
#define RISCV_EXCEPTION_INSTRUCTION_ACCESS 1ULL
#define RISCV_EXCEPTION_ILLEGAL_INSTRUCTION 2ULL
#define RISCV_EXCEPTION_LOAD_ACCESS 5ULL
#define RISCV_EXCEPTION_STORE_ACCESS 7ULL
#define RISCV_EXCEPTION_INSTRUCTION_PAGE 12ULL
#define RISCV_EXCEPTION_LOAD_PAGE 13ULL
#define RISCV_EXCEPTION_STORE_PAGE 15ULL
#define RISCV_INSTRUCTION_WFI 0x10500073ULL
#define PLIC_SUPERVISOR_CONTEXT_OFFSET 0x201000ULL
#define PLIC_CONTEXT_CLAIM_OFFSET 4ULL
#define PLIC_CONTEXT_HART_STRIDE 0x2000ULL

extern void RiscVException__entry(void);

static volatile uint32_t * RiscVException__plic_claim_register(ConstPlicInterruptControllerInfoPtr plic)
{
    Address context = PLIC_SUPERVISOR_CONTEXT_OFFSET + RiscVCPU__physical_hart_id() * PLIC_CONTEXT_HART_STRIDE;
    return (volatile uint32_t *)(plic->virtual_base + context + PLIC_CONTEXT_CLAIM_OFFSET);
}

void RiscVException__install(void)
{
    __asm__ volatile("csrw stvec, %0\n"
                     "csrw sscratch, zero"
                     :
                     : "r"(RiscVException__entry)
                     : "memory");
}

static bool RiscVException__yield_if_parked(KernelPtr kernel, OptionalPtr(ProcessPtr) current_process)
{
    match_optional(
        *current_process,
        some(process)
        {
            if (Process__is_parked(process))
            {
                Scheduler__yield_waiting(kernel->scheduler, process, CPU__id() - 1U);
                return true;
            }
        },
        none_do_nothing()
    );
    return false;
}

static void RiscVException__handle_syscall(ProcessContextPtr context)
{
    KernelPtr kernel = Kernel__instance();
    context->sepc += 4U;
    Optional(ProcessPtr) current_process = Scheduler__save_context(kernel->scheduler, CPU__id() - 1U, context);

    Result(SystemCallResult) result =
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &current_process, context->x[17],
                                    context->x[10], context->x[11], context->x[12], context->x[13], context->x[14]);
    match_result(
        result,
        success(value)
        {
            if (!RiscVException__yield_if_parked(kernel, &current_process))
            {
                context->x[10] = value;
            }
        },
        error(code, message)
        {
            if (!RiscVException__yield_if_parked(kernel, &current_process))
            {
                context->x[10] = code;
            }
        }
    );
}

static ProcessAbortReason RiscVException__abort_reason(uint64_t cause)
{
    if (cause == RISCV_EXCEPTION_INSTRUCTION_ACCESS || cause == RISCV_EXCEPTION_INSTRUCTION_PAGE)
    {
        return PROCESS_ABORT_INSTRUCTION_ABORT;
    }
    if (cause == RISCV_EXCEPTION_ILLEGAL_INSTRUCTION)
    {
        return PROCESS_ABORT_UNDEFINED_INSTRUCTION;
    }
    return PROCESS_ABORT_DATA_ABORT;
}

static void RiscVException__handle_user_wait(ProcessContextPtr context)
{
    KernelPtr kernel = Kernel__instance();
    CPUID cpu = CPU__id() - 1U;
    context->sepc += 4U;
    Scheduler__save_context(kernel->scheduler, cpu, context);
    Scheduler__preempt(kernel->scheduler, cpu, context);
    CPU__wait_for_interrupt();
}

static void RiscVException__abort_user(ProcessContextPtr context, uint64_t cause)
{
    KernelPtr kernel = Kernel__instance();
    Optional(ProcessPtr) current_process = Scheduler__save_context(kernel->scheduler, CPU__id() - 1U, context);
    match_optional(
        current_process,
        some(process)
        {
            ProcessAbort abort;
            abort.reason = RiscVException__abort_reason(cause);
            abort.exception_description = "RISC-V user exception";
            abort.fault_status_description = "not applicable";
            abort.access_description = "not applicable";
            abort.pc = context->sepc;
            abort.fault_address = context->stval;
            abort.exception_code = cause;
            abort.fault_status_code = 0;
            abort.syndrome = cause;
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
            Kernel__panic("RV64 user exception without current process", context);
        }
    );
}

static void RiscVException__dispatch_external(ProcessContextPtr context, bool current_privilege, IRQNumber irq)
{
    KernelPtr kernel = Kernel__instance();
    CPUID cpu = CPU__id() - 1U;
    if (!current_privilege)
    {
        Scheduler__save_context(kernel->scheduler, cpu, context);
    }

    IRQDispatchContext dispatch_context = IRQDispatchContext__new(irq, cpu, current_privilege, context);
    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, irq, &dispatch_context),
        success(status)
        {
            IpcPlatform__signal_event();
            if (!current_privilege && status == IRQ_DISPATCH_RESCHEDULE)
            {
                Scheduler__preempt(kernel->scheduler, cpu, context);
            }
        },
        error(code, message)
        {
            __d_printf(UNHANDLED, "PLIC dispatch error irq=%i code=%i message=%s\n", irq, code, cstr(message));
        }
    );
}

static void RiscVException__handle_external(ProcessContextPtr context, bool current_privilege)
{
    KernelPtr kernel = Kernel__instance();
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            if (system_info.interrupt_controller.kind != INTERRUPT_CONTROLLER_PLIC)
            {
                Kernel__panic("RV64 external interrupt without PLIC", context);
            }

            ConstPlicInterruptControllerInfoPtr plic = &system_info.interrupt_controller.details.plic;
            volatile uint32_t * claim = RiscVException__plic_claim_register(plic);
            IRQNumber irq = *claim;
            if (irq != 0)
            {
                *claim = (uint32_t)irq;
                __asm__ volatile("fence iorw, iorw" ::: "memory");
                RiscVException__dispatch_external(context, current_privilege, irq);
            }
        },
        none()
        {
            Kernel__panic("RV64 external interrupt without system information", context);
        }
    );
}

void RiscVException__handle(ProcessContextPtr context)
{
    bool current_privilege = (context->sstatus & RISCV_SSTATUS_SPP) != 0;
    bool interrupt = (context->scause & RISCV_INTERRUPT_FLAG) != 0;
    uint64_t cause = context->scause & ~RISCV_INTERRUPT_FLAG;

    if (interrupt && cause == RISCV_INTERRUPT_SUPERVISOR_TIMER)
    {
        RiscVCPUTimer__handle_interrupt(context, current_privilege);
        return;
    }
    if (interrupt && cause == RISCV_INTERRUPT_SUPERVISOR_EXTERNAL)
    {
        RiscVException__handle_external(context, current_privilege);
        return;
    }
    if (!interrupt && !current_privilege && cause == RISCV_EXCEPTION_USER_ECALL)
    {
        RiscVException__handle_syscall(context);
        return;
    }
    if (!interrupt && !current_privilege && cause == RISCV_EXCEPTION_ILLEGAL_INSTRUCTION &&
        context->stval == RISCV_INSTRUCTION_WFI)
    {
        RiscVException__handle_user_wait(context);
        return;
    }
    if (!interrupt && !current_privilege &&
        (cause == RISCV_EXCEPTION_INSTRUCTION_ACCESS || cause == RISCV_EXCEPTION_ILLEGAL_INSTRUCTION ||
         cause == RISCV_EXCEPTION_LOAD_ACCESS || cause == RISCV_EXCEPTION_STORE_ACCESS ||
         cause == RISCV_EXCEPTION_INSTRUCTION_PAGE || cause == RISCV_EXCEPTION_LOAD_PAGE ||
         cause == RISCV_EXCEPTION_STORE_PAGE))
    {
        RiscVException__abort_user(context, cause);
        return;
    }

    Kernel__panic("Unhandled RV64 exception", context);
}

#include <stdint.h>

#include "../../kernel/kernel.h"
#include "cpu_timer.h"
#include "exception.h"

#include <stdint.h>

#include "asm_helpers.h"
#include "../../kernel/debug.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/syscall/syscall_dispatcher.h"
#include "io/uart.h"

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

    __d_printf(IRQ, "ELR: %p, SP_ELO: %p\n", get_elr_el1(), read_sp_el0());

    if (id == PPI_CNTV)
    {
        // re-arm for periodic tick
        uint64_t frq; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(frq));
        __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(frq/1000)); // 1ms again
        icc_eoir1(id);
        icc_dir(id);
        isb();
        if (!current_el)
        {
            timer_handler(CPU__id() - 1, kernel->scheduler, context);
        }
        return;
    }
    else if (id == PPI_CNTP)
    {
        uint64_t frq; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(frq));
        __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(frq/1000));
        icc_eoir1(id);
        icc_dir(id);
        isb();

        if (!current_el)
        {
            timer_handler(CPU__id() - 1, kernel->scheduler, context);
        }
        return;
    }
    else if (id == 33)
    {
        __d_printf(IRQ, "irq: %i\n", id);
        Optional(ProcessPtr) interrupted_process = Scheduler__save_context(kernel->scheduler, CPU__id() - 1, context);
        icc_eoir1(id);
        icc_dir(id);
        uart_irq_handler(kernel->io_subsystem);
    }
    else
    {
        __d_printf(IRQ, "irq: %i\n", id);

        __d_printf(UNHANDLED, "unhandled irq: %i\n", id);
    }
}

uint64_t handle_sync(ProcessContextPtr context, bool hcf) {
    uint64_t esr = get_esr_el1();
    uint64_t ec = (esr >> 26) & 0x3F;  // Exception class

    __d_printf(SYSCALL, "sync call: 0x%x\n", ec);

    KernelPtr kernel = Kernel__instance();
    Optional(ProcessPtr) current_process = Scheduler__save_context(kernel->scheduler, CPU__id() - 1, context);

    // Handle synchronous exceptions (e.g., undefined instruction, SVC, data abort)
    switch(ec) {
        case 0x15: // SVC instruction execution
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
                    __d_printf(SYSCALL, "syscall result: res=%p\n", call_result);
                    return call_result;
                });
                IsError(code, message,
                {
                    __d_printf(PANIC, "syscall error: %s code=%i\n", KString__cstr(&message), code);
                    return code;
                });
            End
            return 0;
            break;
        case 0x24: // Data abort from current EL
            __d_println(FAULT, "data abort:");
            /*if (hcf)*/ {
                char buf[128];
                k_sprintf(buf, "data abort call: cpu=%i", CPU__id());
                Kernel__panic(buf, context);
            }
            return 0;
            break;
        case 0x25: // Instruction abort from current EL
            /*if (hcf)*/ {
                char buf2[128];
                k_sprintf(buf2, "instruction abort call: cpu=%i", CPU__id());
                Kernel__panic(buf2, context);
            }
            Kernel__panic("Instruction abort encountered", context);
            return 0;
            break;
        case 0x20:
            __d_printf(PROGRESS, "Abort encountered cpu=%i\n", CPU__id());
            Kernel__panic("Unhandled CPU exception", context);
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
    return handle_sync(context, 0);
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
    return handle_sync(context, 1);
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
    handle_sync(context, 0);
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
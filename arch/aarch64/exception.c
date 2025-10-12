#include <stdint.h>

#include "../../kernel/kernel.h"
#include "cpu_timer.h"
#include "exception.h"
#include "asm_helpers.h"
#include "../../kernel/debug.h"


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

void irq_handler(exception_context_t *ctx)
{
    uint32_t id = icc_iar1();
    __d_printf(DEBUG, "irq: %i\n", id);

    if (id == PPI_CNTV) {
        // re-arm for periodic tick
        uint64_t frq; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(frq));
        __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(frq/1000)); // 1ms again
        timer_handler();
    } else if (id == PPI_CNTP) {
        uint64_t frq; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(frq));
        __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(frq/1000));
        timer_handler();
    } else {
        __d_printf(UNHANDLED, "unhandled irq: %i\n", id);
    }
    icc_eoir1(id);
    icc_dir(id);
}

uint64_t handle_sync(exception_context_t *ctx, bool hcf) {
    uint64_t esr = get_esr_el1();
    uint64_t ec = (esr >> 26) & 0x3F;  // Exception class

    __d_printf(DEBUG, "sync call: 0x%x\n", ec);

    // Handle synchronous exceptions (e.g., undefined instruction, SVC, data abort)
    switch(ec) {
        case 0x15: // SVC instruction execution
            __d_printf(DEBUG, "system call: 0x%x\n", ctx->x[8]);
            if (ctx->x[8] == 0x21) {
                k_println((const char *)ctx->x[0]);
            }
            return 1;
            break;
        case 0x24: // Data abort from current EL
            __d_println(FAULT, "data abort: 0x%x");
            if (hcf) {
                char buf[128];
                k_sprintf(buf, "data abort call: cpu=%i", CPU__id());
                Kernel__panic(buf, ctx);
            }
            return 0;
            break;
        case 0x25: // Instruction abort from current EL
            if (hcf) {
                char buf[128];
                k_sprintf(buf, "instruction abort call: cpu=%i", CPU__id());
                Kernel__panic(buf, ctx);
            }
            return 0;
            break;
        case 0x20:
            // __d_printf(PROGRESS, "BRK encountered cpu=%i\n", CPU__id())
            return 0;
            break;
        default:
            // Handle other synchronous exceptions
            Kernel__panic("Unhandled CPU exception", ctx);
            return 0;
            break;
    }
}

// Current EL with SP0 handlers
uint64_t c_sync_handler_current_el_sp0(exception_context_t *ctx) {
    __d_println(DEBUG, "\nc_sync_handler_current_el_sp0");
    return handle_sync(ctx, 0);
}

void c_irq_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle IRQ interrupt
    // Process pending interrupts from interrupt controller
    __d_println(DEBUG, "\nc_irq_handler_current_el_sp0");
    irq_handler(ctx);
}

void c_fiq_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle FIQ (fast interrupt)
    __d_println(DEBUG, "\nc_fiq_handler_current_el_sp0");
}

void c_serror_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle system error (SError)
    // This is typically fatal
    __d_println(DEBUG, "\nc_serror_handler_current_el_sp0");
}

// Current EL with SPx handlers
uint64_t c_sync_handler_current_el_spx(exception_context_t *ctx) {
    __d_println(DEBUG, "\nc_sync_handler_current_el_spx");
    return handle_sync(ctx, 1);
    // Handle synchronous exceptions from current EL
}

void c_irq_handler_current_el_spx(exception_context_t *ctx) {
    // Handle IRQ from current EL with SPx
    __d_println(DEBUG, "\nc_irq_handler_current_el_spx");
    irq_handler(ctx);
}

void c_fiq_handler_current_el_spx(exception_context_t *ctx) {
    // Handle FIQ from current EL with SPx
    __d_println(DEBUG, "\nc_fiq_handler_current_el_spx");
}

void c_serror_handler_current_el_spx(exception_context_t *ctx) {
    // Handle SError from current EL with SPx
    __d_println(DEBUG, "\nc_serror_handler_current_el_spx");
}

// Lower EL using AArch64 handlers
void c_sync_handler_lower_el_aarch64(exception_context_t *ctx) {
    __d_println(DEBUG, "\nc_sync_handler_lower_el_aarch64");
    handle_sync(ctx, 0);
}

void c_irq_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle IRQ from lower EL (AArch64)
    __d_println(DEBUG, "\nc_irq_handler_current_el_aarch64");
    irq_handler(ctx);
}

void c_fiq_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle FIQ from lower EL (AArch64)
    __d_println(DEBUG, "\nc_fiq_handler_current_el_aarch64");
}

void c_serror_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle SError from lower EL (AArch64)
    __d_println(DEBUG, "\nc_serror_handler_current_el_aarch64");
}

// Lower EL using AArch32 handlers
void c_sync_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle synchronous exceptions from AArch32 lower EL
    __d_println(DEBUG, "\nc_sync_handler_lower_el_aarch32");
}

void c_irq_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle IRQ from lower EL (AArch32)
    __d_println(DEBUG, "\nc_irq_handler_lower_el_aarch32");
    irq_handler(ctx);
}

void c_fiq_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle FIQ from lower EL (AArch32)
    __d_println(DEBUG, "\nc_fiq_handler_lower_el_aarch32");
}

void c_serror_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle SError from lower EL (AArch32)
    __d_println(DEBUG, "\nc_serror_handler_lower_el_aarch32");
}

// Initialize vector table
void init_vector_table(void) {
    extern void *vector_table;
    uint64_t vbar = (uint64_t)&vector_table;
    
    // Set VBAR_EL1 (or appropriate EL)
    __asm__ volatile("msr vbar_el1, %0" : : "r" (vbar));
    __asm__ volatile("isb");
}
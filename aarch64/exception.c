#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>

#include "../kernel/kernel.h"
#include "cpu_timer.h"

inline uint32_t icc_iar1(void){ uint64_t x; __asm__ volatile("mrs %0, ICC_IAR1_EL1":"=r"(x)); return (uint32_t)x; }
inline void icc_eoir1(uint32_t id){ __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"((uint64_t)id)); }
inline void icc_dir(uint32_t id){   __asm__ volatile("msr ICC_DIR_EL1,   %0" :: "r"((uint64_t)id)); }

// Structure to hold saved register context
typedef struct {
    uint64_t x[31];  // x0-x30
    uint64_t sp;
    uint64_t pc;     // ELR
    uint64_t pstate; // SPSR
} exception_context_t;

inline uint64_t get_current_el(void) {
    uint64_t el;
    __asm__ __volatile__("mrs %0, CurrentEL" : "=r" (el));
    return (el >> 2) & 3;
}

inline uint64_t read_esr_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, esr_el1" : "=r"(x));
    return x;
}

inline uint64_t read_elr_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, elr_el1" : "=r"(x));
    return x;
}

inline uint64_t read_far_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, far_el1" : "=r"(x));
    return x;
}

inline uint64_t read_spsr_el1(void) {
    uint64_t x;
    __asm__ __volatile__("mrs %0, spsr_el1" : "=r"(x));
    return x;
}

void dump_state(void) {
    uint64_t esr, elr, far, sctlr;
    
    __asm__ volatile("mrs %0, esr_el1" : "=r" (esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r" (elr));
    __asm__ volatile("mrs %0, far_el1" : "=r" (far));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
    
    uint32_t ec = (esr >> 26) & 0x3F;
    
    __d_printf(FAULT, "Exception Class: %x\n", ec);
    __d_printf(FAULT, "ESR: %x\n", esr);
    __d_printf(FAULT, "Fault Address (FAR): %x\n", far);
    __d_printf(FAULT, "Exception Link Register (ELR): %x\n", elr);
    
    if (ec == 0x21) {
        __d_println(FAULT, "Instruction Abort from current EL");
    }
}

void vectors_init(void) {
    extern void *vector_table;
    uint64_t vbar = (uint64_t)&vector_table;
    
    // Set VBAR_EL1 (or appropriate EL)
    __asm__ volatile("msr vbar_el1, %0" : : "r" (vbar));
    __asm__ volatile("isb");
}

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

void __panic(exception_context_t *ctx, const char *error) {
    uint64_t esr = read_esr_el1();
    uint64_t elr = read_elr_el1();
    uint64_t far = read_far_el1();
    uint64_t spsr = read_spsr_el1();
    uint64_t lr = ctx->x[30];     // link register saved in your trap frame

    __d_printf(PANIC,
      "IABT: ESR=0x%x EC=0x%x ISS=0x%x ELR=0x%x LR=0x%x FAR=0x%x\n",
      esr, (esr>>26)&0x3f, esr&0x01ffffff, elr, lr, far);

    panic(error);
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
                k_sprintf(buf, "data abort call: cpu=%i", cpu_id());
                __panic(ctx, buf);
            }
            return 0;
            break;
        case 0x25: // Instruction abort from current EL
            __d_println(PANIC, "instruction abort call: ");
            if (hcf) {
                char buf[128];
                k_sprintf(buf, "instruction abort call: cpu=%i", cpu_id());
                __panic(ctx, buf);
            }
            return 0;
            break;
        default:
            // Handle other synchronous exceptions
            __d_printf(UNHANDLED, "unhandled call: %i\n", ec);
            return 0;
            break;
    }
}

// Current EL with SP0 handlers
uint64_t c_sync_handler_current_el_sp0(exception_context_t *ctx) {
    __d_println(DEBUG, "c_sync_handler_current_el_sp0");
    return handle_sync(ctx, 0);
}

void c_irq_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle IRQ interrupt
    // Process pending interrupts from interrupt controller
    __d_println(DEBUG, "c_irq_handler_current_el_sp0");
    irq_handler(ctx);
}

void c_fiq_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle FIQ (fast interrupt)
    __d_println(DEBUG, "c_fiq_handler_current_el_sp0");
}

void c_serror_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle system error (SError)
    // This is typically fatal
    __d_println(DEBUG, "c_serror_handler_current_el_sp0");
}

// Current EL with SPx handlers
uint64_t c_sync_handler_current_el_spx(exception_context_t *ctx) {
    __d_println(DEBUG, "c_sync_handler_current_el_spx");
    return handle_sync(ctx, 1);
    // Handle synchronous exceptions from current EL
}

void c_irq_handler_current_el_spx(exception_context_t *ctx) {
    // Handle IRQ from current EL with SPx
    __d_println(DEBUG, "c_irq_handler_current_el_spx");
    irq_handler(ctx);
}

void c_fiq_handler_current_el_spx(exception_context_t *ctx) {
    // Handle FIQ from current EL with SPx
    __d_println(DEBUG, "c_fiq_handler_current_el_spx");
}

void c_serror_handler_current_el_spx(exception_context_t *ctx) {
    // Handle SError from current EL with SPx
    __d_println(DEBUG, "c_serror_handler_current_el_spx");
}

// Lower EL using AArch64 handlers
void c_sync_handler_lower_el_aarch64(exception_context_t *ctx) {
    __d_println(DEBUG, "c_sync_handler_lower_el_aarch64");
    handle_sync(ctx, 0);
}

void c_irq_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle IRQ from lower EL (AArch64)
    __d_println(DEBUG, "c_irq_handler_current_el_aarch64");
    irq_handler(ctx);
}

void c_fiq_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle FIQ from lower EL (AArch64)
    __d_println(DEBUG, "c_fiq_handler_current_el_aarch64");
}

void c_serror_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle SError from lower EL (AArch64)
    __d_println(DEBUG, "c_serror_handler_current_el_aarch64");
}

// Lower EL using AArch32 handlers
void c_sync_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle synchronous exceptions from AArch32 lower EL
    __d_println(DEBUG, "c_sync_handler_lower_el_aarch32");
}

void c_irq_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle IRQ from lower EL (AArch32)
    __d_println(DEBUG, "c_irq_handler_lower_el_aarch32");
    irq_handler(ctx);
}

void c_fiq_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle FIQ from lower EL (AArch32)
    __d_println(DEBUG, "c_fiq_handler_lower_el_aarch32");
}

void c_serror_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle SError from lower EL (AArch32)
    __d_println(DEBUG, "c_serror_handler_lower_el_aarch32");
}

// Initialize vector table
void init_vector_table(void) {
    extern void *vector_table;
    uint64_t vbar = (uint64_t)&vector_table;
    
    // Set VBAR_EL1 (or appropriate EL)
    __asm__ volatile("msr vbar_el1, %0" : : "r" (vbar));
    __asm__ volatile("isb");
}
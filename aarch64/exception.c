#include <stdint.h>
#include "../kernel/kernel.h"

// Structure to hold saved register context
typedef struct {
    uint64_t x[31];  // x0-x30
    uint64_t sp;
    uint64_t pc;     // ELR
    uint64_t pstate; // SPSR
} exception_context_t;

static inline uint64_t get_current_el(void) {
    uint64_t el;
    asm volatile("mrs %0, CurrentEL" : "=r" (el));
    return (el >> 2) & 3;
}

void dump_state(void) {
    uint64_t esr, elr, far, sctlr;
    
    __asm__ volatile("mrs %0, esr_el1" : "=r" (esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r" (elr));
    __asm__ volatile("mrs %0, far_el1" : "=r" (far));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r" (sctlr));
    
    uint32_t ec = (esr >> 26) & 0x3F;
    
    k_printf("Exception Class: %x\n", ec);
    k_printf("ESR: %x\n", esr);
    k_printf("Fault Address (FAR): %x\n", far);
    k_printf("Exception Link Register (ELR): %x\n", elr);
    
    if (ec == 0x21) {
        k_println("Instruction Abort from current EL");
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
static inline uint64_t get_esr_el1(void) {
    uint64_t esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r" (esr));
    return esr;
}

static inline uint64_t get_far_el1(void) {
    uint64_t far;
    __asm__ volatile("mrs %0, far_el1" : "=r" (far));
    return far;
}

static inline uint64_t get_elr_el1(void) {
    uint64_t elr;
    __asm__ volatile("mrs %0, elr_el1" : "=r" (elr));
    return elr;
}


uint64_t handle_sync(exception_context_t *ctx, bool hcf) {
    uint64_t esr = get_esr_el1();
    uint64_t ec = (esr >> 26) & 0x3F;  // Exception class

    k_printf("sync call: %i\n", ec);
    // k_printf("Args: arg0=%i, arg1=%i, arg2=%i\n", (int)ctx->x[0], (int)ctx->x[1], (int)ctx->x[2]);
        
    // Handle synchronous exceptions (e.g., undefined instruction, SVC, data abort)
    switch(ec) {
        case 0x15: // SVC instruction execution
            k_printf("system call: %i\n", ctx->x[8]);
            return 1;
            break;
        case 0x24: // Data abort from current EL
            k_println("data abort: ");
            return 0;
            break;
        case 0x25: // Instruction abort from current EL
            k_println("instruction abort call: ");
            if (hcf) {
                panic("instruction abort call: ");
            }
            return 0;
            break;
        default:
            // Handle other synchronous exceptions
            k_printf("unhandled call: %i\n", ec);
            return 0;
            break;
    }
    

}

// Current EL with SP0 handlers
uint64_t c_sync_handler_current_el_sp0(exception_context_t *ctx) {
    k_println("c_sync_handler_current_el_sp0");
    return handle_sync(ctx, 0);
}

void c_irq_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle IRQ interrupt
    // Process pending interrupts from interrupt controller
    k_println("c_irq_handler_current_el_sp0");
}

void c_fiq_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle FIQ (fast interrupt)
    k_println("c_fiq_handler_current_el_sp0");
}

void c_serror_handler_current_el_sp0(exception_context_t *ctx) {
    // Handle system error (SError)
    // This is typically fatal
    k_println("c_serror_handler_current_el_sp0");    
}

// Current EL with SPx handlers
uint64_t c_sync_handler_current_el_spx(exception_context_t *ctx) {
    k_println("c_sync_handler_current_el_spx");
    return handle_sync(ctx, 1);
    // Handle synchronous exceptions from current EL
}

void c_irq_handler_current_el_spx(exception_context_t *ctx) {
    // Handle IRQ from current EL with SPx
    k_println("c_irq_handler_current_el_spx");
}

void c_fiq_handler_current_el_spx(exception_context_t *ctx) {
    // Handle FIQ from current EL with SPx
    k_println("c_fiq_handler_current_el_spx");
}

void c_serror_handler_current_el_spx(exception_context_t *ctx) {
    // Handle SError from current EL with SPx
    k_println("c_serror_handler_current_el_spx");
}

// Lower EL using AArch64 handlers
void c_sync_handler_lower_el_aarch64(exception_context_t *ctx) {
    k_println("c_sync_handler_lower_el_aarch64");
    handle_sync(ctx, 0);
}

void c_irq_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle IRQ from lower EL (AArch64)
    k_println("c_irq_handler_current_el_aarch64");
}

void c_fiq_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle FIQ from lower EL (AArch64)
    k_println("c_fiq_handler_current_el_aarch64");
}

void c_serror_handler_lower_el_aarch64(exception_context_t *ctx) {
    // Handle SError from lower EL (AArch64)
    k_println("c_serror_handler_current_el_aarch64");
}

// Lower EL using AArch32 handlers
void c_sync_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle synchronous exceptions from AArch32 lower EL
    k_println("c_sync_handler_lower_el_aarch32");
}

void c_irq_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle IRQ from lower EL (AArch32)
    k_println("c_irq_handler_lower_el_aarch32");
}

void c_fiq_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle FIQ from lower EL (AArch32)
    k_println("c_fiq_handler_lower_el_aarch32");
}

void c_serror_handler_lower_el_aarch32(exception_context_t *ctx) {
    // Handle SError from lower EL (AArch32)
    k_println("c_serror_handler_lower_el_aarch32");
}

// Initialize vector table
void init_vector_table(void) {
    extern void *vector_table;
    uint64_t vbar = (uint64_t)&vector_table;
    
    // Set VBAR_EL1 (or appropriate EL)
    __asm__ volatile("msr vbar_el1, %0" : : "r" (vbar));
    __asm__ volatile("isb");
}
#include <stdint.h>
#include "../../kernel/debug.h"
#include "asm_helpers.h"
#include "exception.h"
#include "exception_decode.h"
#include "process.h"

void Kernel__halt() {
    while(1) {
        __asm__ __volatile__("wfi");
    }
}

void Kernel__dump_state(ProcessContext * ctx) {
    uint64_t esr = read_esr_el1();

    uint32_t ec = (esr >> 26) & 0x3F;

    __d_printf(FAULT, "Fault: %s\n", AArch64Exception__class_name(ec));
    __d_printf(FAULT, "Exception Class (EC): %p\n", ec);
    __d_printf(FAULT, "ESR: %p\n", esr);
    if (AArch64Exception__has_fault_status(ec))
    {
        __d_printf(
            FAULT,
            "Fault Status (FSC): %p %s\n",
            AArch64Exception__fault_status_code(esr),
            AArch64Exception__fault_status_name(esr)
        );
        if (ec == 0x24 || ec == 0x25)
        {
            __d_printf(FAULT, "Access: %s\n", AArch64Exception__data_abort_access_name(esr));
        }
    }
    __d_printf(FAULT, "Fault Address (FAR): %p\n", read_far_el1());
    __d_printf(FAULT, "Exception Link Register (ELR): %p\n", read_elr_el1());
    __d_printf(FAULT, "SCTLR: %p\n", read_sctlr_el1());
    __d_printf(FAULT, "MAIR: %p\n", read_mair_el1());
    __d_printf(FAULT, "TCR: %p\n", read_tcr_el1());
    __d_printf(FAULT, "TBBR0: %p\n", read_ttbr0_el1());
    __d_printf(FAULT, "TBBR1: %p\n", read_ttbr1_el1());

    if (ctx) {
        __d_printf(FAULT, "PC: %p\n", ctx->elr);
        __d_printf(FAULT, "SP: %p\n", ctx->sp);
        __d_printf(FAULT, "PSTATE: %p\n", ctx->spsr);
        int width = 6;
        for (int i = 0; i < 31; i++) {
            if (i % width == 0) {
                __d_println(FAULT, "");
            }
            __d_printf(FAULT, "X[%i]: %p  ", i, ctx->x[i]);
        }
    }
    else {
        __d_println(FAULT, "No context provided");
    }
}

void Kernel__dump_context(ProcessContext * ctx) {
    __d_printf(FAULT, "Context: %p\n", ctx);
    __d_printf(FAULT, "PC: %p\n", ctx->elr);
    __d_printf(FAULT, "SP: %p\n", ctx->sp);
    __d_printf(FAULT, "PSTATE: %p\n", ctx->spsr);
}

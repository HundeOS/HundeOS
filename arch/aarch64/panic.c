#include <stdint.h>
#include "../../kernel/debug.h"
#include "asm_helpers.h"
#include "exception.h"
#include "process.h"

void Kernel__halt() {
    while(1) {
        __asm__ __volatile__("wfi");
    }
}

void Kernel__dump_state(ProcessContext * ctx) {
    uint64_t esr = read_esr_el1();
    uint64_t elr = read_elr_el1();
    uint64_t far = read_far_el1();
    uint64_t sctlr = read_sctlr_el1();

    uint32_t ec = (esr >> 26) & 0x3F;

    __d_printf(FAULT, "Exception Class: %p\n", ec);
    __d_printf(FAULT, "ESR: %p\n", read_esr_el1());
    __d_printf(FAULT, "Fault Address (FAR): %p\n", read_far_el1());
    __d_printf(FAULT, "Exception Link Register (ELR): %p\n", read_elr_el1());
    __d_printf(FAULT, "SCTLR: %p\n", read_sctlr_el1());
    __d_printf(FAULT, "MAIR: %p\n", read_mair_el1());
    __d_printf(FAULT, "TCR: %p\n", read_tcr_el1());
    __d_printf(FAULT, "TBBR0: %p\n", read_ttbr0_el1());
    __d_printf(FAULT, "TBBR1: %p\n", read_ttbr1_el1());

    if (ctx) {
        __d_printf(FAULT, "PC: %p\n", ctx->pc);
        __d_printf(FAULT, "SP: %p\n", ctx->sp);
        __d_printf(FAULT, "PSTATE: %p\n", ctx->pstate);
        int width = 6;
        for (int i = 0; i < 31; i++) {
            if (i % width == 0) {
                __d_println(FAULT, "");
            }
            __d_printf(FAULT, "X[%i]: %p  ", i, ctx->x[i]);
        }

        if (ec == 0x21) {
            __d_println(FAULT, "Instruction Abort from current EL");
        }
    }
    else {
        __d_println(FAULT, "No context provided");
    }
}
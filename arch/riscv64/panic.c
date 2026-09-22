#include "../../kernel/arch/panic.h"
#include "../../kernel/debug.h"
#include "process.h"

#include <stdbool.h>

void Kernel__halt(void)
{
    while (true)
    {
        __asm__ volatile("wfi");
    }
}

void Kernel__dump_state(ProcessContextPtr context)
{
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    __d_printf(FAULT, "SATP: %p\n", satp);
    if (context != NULL)
    {
        __d_printf(FAULT, "SCAUSE: %p\n", context->scause);
        __d_printf(FAULT, "STVAL: %p\n", context->stval);
        Kernel__dump_context(context);
    }
}

void Kernel__dump_context(ProcessContextPtr context)
{
    __d_printf(FAULT, "Context: %p\n", context);
    __d_printf(FAULT, "PC: %p\n", context->sepc);
    __d_printf(FAULT, "SP: %p\n", context->x[2]);
    __d_printf(FAULT, "SSTATUS: %p\n", context->sstatus);
    for (uint64_t index = 1; index < 32; index++)
    {
        __d_printf(FAULT, "X[%i]: %p\n", index, context->x[index]);
    }
}

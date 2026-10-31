#include "../../kernel/arch/panic.h"
#include "../../kernel/arch/basic_io.h"
#include "../../kernel/debug.h"
#include "process.h"

#include <stdbool.h>

static void Kernel__panic_console_value(const char * label, uint64_t value)
{
    char hexadecimal[19] = "0x0000000000000000";
    const char digits[] = "0123456789abcdef";

    for (uint64_t index = 0; index < 16; index++)
    {
        hexadecimal[17 - index] = digits[value & 0xfU];
        value >>= 4U;
    }

    BasicIO__puts(label);
    BasicIO__puts(hexadecimal);
    BasicIO__puts("\n");
}

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
    Kernel__panic_console_value("SATP: ", satp);
    __d_printf(FAULT, "SATP: %p\n", satp);
    if (context != NULL)
    {
        Kernel__panic_console_value("SCAUSE: ", context->scause);
        Kernel__panic_console_value("STVAL: ", context->stval);
        Kernel__panic_console_value("PC: ", context->sepc);
        Kernel__panic_console_value("RA: ", context->x[1]);
        Kernel__panic_console_value("SP: ", context->x[2]);
        Kernel__panic_console_value("A0: ", context->x[10]);
        Kernel__panic_console_value("A2: ", context->x[12]);
        Kernel__panic_console_value("A4: ", context->x[14]);
        Kernel__panic_console_value("SSTATUS: ", context->sstatus);
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

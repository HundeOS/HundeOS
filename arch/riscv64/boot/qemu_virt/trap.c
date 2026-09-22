#include <stdint.h>

#include "platform.h"

#define RISCV_EXCEPTION_BREAKPOINT 3ULL

extern void RiscVTrap__entry(void);

static volatile uint64_t last_cause;
static volatile uint64_t last_value;

void RiscVTrap__install(void)
{
    __asm__ volatile("csrw stvec, %0" : : "r"(RiscVTrap__entry) : "memory");
}

uint64_t RiscVTrap__handle(uint64_t cause, uint64_t exception_pc, uint64_t value)
{
    last_cause = cause;
    last_value = value;

    if (cause == RISCV_EXCEPTION_BREAKPOINT)
    {
        volatile uint16_t * instruction = (volatile uint16_t *)exception_pc;
        uint64_t instruction_size = ((*instruction & 0x3U) == 0x3U) ? 4U : 2U;
        return exception_pc + instruction_size;
    }

    RiscVUart__puts("Unexpected S-mode trap cause=");
    RiscVUart__put_hex(cause);
    RiscVUart__puts(" epc=");
    RiscVUart__put_hex(exception_pc);
    RiscVUart__puts(" stval=");
    RiscVUart__put_hex(value);
    RiscVUart__puts("\n");
    SBI__shutdown();

    for (;;)
    {
        __asm__ volatile("wfi");
    }
}

uint64_t RiscVTrap__last_cause(void)
{
    return last_cause;
}

uint64_t RiscVTrap__last_value(void)
{
    return last_value;
}

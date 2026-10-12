#include <stdint.h>

#include "platform.h"

extern void RiscVTrap__entry(void);

void RiscVTrap__install(void)
{
    __asm__ volatile("csrw stvec, %0" : : "r"(RiscVTrap__entry) : "memory");
}

uint64_t RiscVTrap__handle(uint64_t cause, uint64_t exception_pc, uint64_t value)
{
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

#include <stdint.h>

#include "../../../../kernel/arch/system.h"
#include "../../../../kernel/arch/cpu.h"

void System__shutdown()
{
    uint64_t arguments[2] = {0x20026u, 0};
    register uint64_t operation __asm__("x0") = 0x18u;
    register uint64_t argument __asm__("x1") = (uint64_t)arguments;
    __asm__ volatile("dsb sy; isb; hlt #0xf000" : : "r"(operation), "r"(argument) : "memory");
    CPU__halt_all();
}

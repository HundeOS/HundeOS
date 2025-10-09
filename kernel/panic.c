#include "panic.h"
#include "print.h"
#include "arch/cpu.h"

void Kernel__panic(const char * message, exception_context_t * context) {
    k_printf("Kernel panic: %s\n", message);
    k_println("System halted");

    Kernel__dump_state(context);
    CPU__halt_all();
}

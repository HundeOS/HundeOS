#include "halt.h"
#include "print.h"

void panic(const char * message, exception_context_t * context) {
    k_printf("Kernel panic: %s\n", message);
    k_println("System halted");

    dump_state(context);
    halt();
}
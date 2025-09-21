#include "halt.h"
#include "print.h"

void panic(const char * message) {
    k_printf("Kernel panic: %s\n", message);
    k_println("System halted");

    dump_state();
    halt();
}
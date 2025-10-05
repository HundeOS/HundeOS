#include "cpu_timer.h"

#include "debug.h"
#include "arch/cpu.h"

void timer_handler() {
    __d_printf(DEBUG, "timer_handler for %i\n", CPU__id());
}

#include "cpu_timer.h"

#include "debug.h"
#include "arch/cpu.h"
#include "scheduler/scheduler.h"

void timer_handler(CPUID cpu_id, SchedulerPtr scheduler, ProcessContextPtr context)
{
    __d_printf(TIMER_IRQ, "timer_handler for %i\n", cpu_id);
    CPU__check_halt();

    Optional(ContextSwitch) context_switch = Scheduler__interrupt(scheduler, cpu_id, context);
    ContextSwitch__switch(context_switch);
}

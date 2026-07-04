#include "cpu_timer.h"

#include "arch/cpu.h"
#include "scheduler/scheduler.h"


void timer_handler(CPUID cpu_id, SchedulerPtr scheduler, ProcessContextPtr context)
{
    CPU__check_halt();

    Scheduler__preempt(scheduler, cpu_id, context);
}

#include "cpu_timer.h"

#include "arch/cpu.h"
#include "kernel_core.h"
#include "kernel_init.h"
#include "scheduler/scheduler.h"

void timer_handler(CPUID cpu_id, SchedulerPtr scheduler, ProcessContextPtr context)
{
    CPU__check_halt();

    if (Scheduler__should_preempt_on_timer(scheduler, cpu_id))
    {
        Scheduler__preempt(scheduler, cpu_id, context);
    }
}

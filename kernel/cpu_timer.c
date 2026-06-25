#include "cpu_timer.h"

#include "debug.h"
#include "arch/cpu.h"
#include "scheduler/scheduler.h"
#include "service/service.h"

static uint64_t timer_interrupt_count = 0;

void timer_handler(CPUID cpu_id, SchedulerPtr scheduler, ProcessContextPtr context)
{
    __d_printf(TIMER_IRQ, "timer_handler for %i\n", cpu_id);
    CPU__check_halt();

    timer_interrupt_count++;
    Service__send_timer_tick_events(scheduler, timer_interrupt_count);

    Scheduler__preempt(scheduler, cpu_id, context);
}

#include "cpu_timer.h"

#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "kernel_core.h"
#include "kernel_init.h"
#include "scheduler/scheduler.h"

#define CPUTIMER_DEFAULT_HZ 1000U

uint64_t CPUTimer__interval(uint64_t frequency, uint64_t hz)
{
    uint64_t target_hz = hz == 0 ? CPUTIMER_DEFAULT_HZ : hz;
    uint64_t interval = frequency / target_hz;
    return interval == 0 ? 1 : interval;
}

Result(IRQDispatchStatus) CPUTimer__handle_irq(IRQDispatchContextPtr context, KernelPtr kernel)
{
    CPUTimer__rearm(context->irq);

    if (context->current_el)
    {
        return Success(IRQDispatchStatus, IRQ_DISPATCH_HANDLED);
    }

    CPU__check_halt();

    if (Scheduler__should_preempt_on_timer(kernel->scheduler, context->cpu))
    {
        Scheduler__preempt(kernel->scheduler, context->cpu, &Some(ProcessContextPtr, context->process_context));
    }
    return Success(IRQDispatchStatus, IRQ_DISPATCH_HANDLED);
}

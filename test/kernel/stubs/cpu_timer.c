#include "cpu_timer.h"

#include "../../../kernel/arch/cpu_timer.h"

static uint64_t rearm_count;
static IRQNumber last_irq;
static uint64_t suspend_count;
static uint64_t resume_count;

void CPUTimer__rearm(IRQNumber irq)
{
    rearm_count++;
    last_irq = irq;
}

void CPUTimer__suspend(void)
{
    suspend_count++;
}

void CPUTimer__resume(void)
{
    resume_count++;
}

void CPUTimerStub__reset(void)
{
    rearm_count = 0;
    last_irq = 0;
    suspend_count = 0;
    resume_count = 0;
}

uint64_t CPUTimerStub__rearm_count(void)
{
    return rearm_count;
}

IRQNumber CPUTimerStub__last_irq(void)
{
    return last_irq;
}

uint64_t CPUTimerStub__suspend_count(void)
{
    return suspend_count;
}

uint64_t CPUTimerStub__resume_count(void)
{
    return resume_count;
}

#include "cpu_timer.h"

#include "../../../kernel/arch/cpu_timer.h"

static uint64_t rearm_count;
static IRQNumber last_irq;

void CPUTimer__rearm(IRQNumber irq)
{
    rearm_count++;
    last_irq = irq;
}

void CPUTimerStub__reset(void)
{
    rearm_count = 0;
    last_irq = 0;
}

uint64_t CPUTimerStub__rearm_count(void)
{
    return rearm_count;
}

IRQNumber CPUTimerStub__last_irq(void)
{
    return last_irq;
}

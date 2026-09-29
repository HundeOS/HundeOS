#include "ipc/ipc_platform.h"

static uint64_t timer_frequency;

IpcDoorbellPtr IpcPlatform__local_doorbell(void)
{
    return (IpcDoorbellPtr)IPC_DOORBELL_ADDRESS;
}

void IpcPlatform__configure_wait(BootWaitMode mode)
{
}

void IpcPlatform__configure_timer_frequency(uint64_t frequency)
{
    timer_frequency = frequency;
}

uint64_t IpcPlatform__monotonic_usec(void)
{
    uint64_t ticks;
    __asm__ __volatile__("rdtime %0" : "=r"(ticks)::"memory");
    if (timer_frequency == 0)
    {
        return 0;
    }
    return ticks / timer_frequency * 1000000ULL + ticks % timer_frequency * 1000000ULL / timer_frequency;
}

void IpcPlatform__relax(void)
{
    __asm__ __volatile__("nop" ::: "memory");
}

void IpcPlatform__signal_event(void)
{
    __asm__ __volatile__("fence rw, rw" ::: "memory");
}

IpcEventGeneration IpcPlatform__prepare_wait(ConstIpcEventPtr event)
{
    IpcEventGeneration generation;
    __asm__ __volatile__("lr.d.aq %0, (%1)" : "=r"(generation) : "r"(event) : "memory");
    return generation;
}

void IpcPlatform__wait_event(ConstIpcEventPtr event, IpcEventGeneration observed)
{
    if (!IpcEvent__changed(event, observed))
    {
        __asm__ __volatile__("wrs.nto" ::: "memory");
    }
}

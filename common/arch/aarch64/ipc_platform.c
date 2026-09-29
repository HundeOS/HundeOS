#include "ipc/ipc_platform.h"

static BootWaitMode wait_mode = BOOT_WAIT_SPIN;

IpcDoorbellPtr IpcPlatform__local_doorbell(void)
{
    return (IpcDoorbellPtr)IPC_DOORBELL_ADDRESS;
}

void IpcPlatform__configure_wait(BootWaitMode mode)
{
    wait_mode = mode;
}

void IpcPlatform__configure_timer_frequency(uint64_t frequency)
{
}

uint64_t IpcPlatform__monotonic_usec(void)
{
    uint64_t counter;
    uint64_t frequency;
    __asm__ __volatile__("isb; mrs %0, cntvct_el0" : "=r"(counter)::"memory");
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(frequency));
    return counter / frequency * 1000000ULL + counter % frequency * 1000000ULL / frequency;
}

void IpcPlatform__relax(void)
{
    __asm__ __volatile__("yield" ::: "memory");
}

void IpcPlatform__signal_event(void)
{
    __asm__ __volatile__("dsb ishst; sev" ::: "memory");
}

IpcEventGeneration IpcPlatform__prepare_wait(ConstIpcEventPtr event)
{
    return IpcEvent__generation(event);
}

void IpcPlatform__wait_event(ConstIpcEventPtr event, IpcEventGeneration observed)
{
    if (IpcEvent__changed(event, observed))
    {
        return;
    }
    if (wait_mode == BOOT_WAIT_WFE)
    {
        __asm__ __volatile__("wfe" ::: "memory");
        return;
    }
    IpcPlatform__relax();
}

#include <stdint.h>
#include "cpu.h"
#include <unistd.h>

#include "../../../kernel/all_defs.h"

static CPUID cpu_id = 0;
static uint64_t cpu_count = MAX_CPUS;
static CPUID rescheduled_cpu;
static uint64_t reschedule_count;
static uint64_t wait_count;

void reset_cpu_id()
{
    cpu_id = 0;
}

void reset_cpu_count()
{
    cpu_count = MAX_CPUS;
}

void reset_cpu_reschedule(void)
{
    rescheduled_cpu = 0;
    reschedule_count = 0;
}

void set_cpu_id(CPUID id)
{
    cpu_id = id;
}

void set_cpu_count(uint64_t count)
{
    cpu_count = count;
}

void CPU__yield()
{
    usleep(1000);
}

CPUID CPU__id()
{
    return cpu_id;
}

bool CPU__is_boot_cpu(void)
{
    return cpu_id == 0;
}

void CPU__noop()
{
}

void CPU__wait()
{
}

void CPU__signal_event()
{
}

void CPU__request_reschedule(CPUID cpu)
{
    rescheduled_cpu = cpu;
    reschedule_count++;
}

CPUID CPUStub__rescheduled_cpu(void)
{
    return rescheduled_cpu;
}

uint64_t CPUStub__reschedule_count(void)
{
    return reschedule_count;
}

void CPUStub__reset_wait_count(void)
{
    wait_count = 0;
}

uint64_t CPUStub__wait_count(void)
{
    return wait_count;
}

uint64_t CPU__count()
{
    return cpu_count;
}

void CPU__wait_for_interrupts()
{
}

void CPU__wait_for_interrupt(void)
{
    wait_count++;
}

void CPU__check_halt()
{
}

void CPU__halt_all()
{
}

void CPU__assign_id(CPUID id)
{
    cpu_id = id;
}

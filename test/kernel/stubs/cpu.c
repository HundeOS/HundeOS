#include <stdint.h>
#include "cpu.h"
#include <unistd.h>

#include "../../../kernel/all_defs.h"

static CPUID cpu_id = 0;
static uint64_t cpu_count = MAX_CPUS;

void reset_cpu_id()
{
    cpu_id = 0;
}

void reset_cpu_count()
{
    cpu_count = MAX_CPUS;
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

void CPU__noop()
{
}

void CPU__wait()
{
}

void CPU__signal_event()
{
}

uint64_t CPU__count()
{
    return cpu_count;
}

void CPU__wait_for_interrupts()
{
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

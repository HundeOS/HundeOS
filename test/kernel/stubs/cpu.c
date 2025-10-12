#include <stdint.h>
#include "cpu.h"
#include <unistd.h>

static CPUID cpu_id = 0;

void reset_cpu_id()
{
    cpu_id = 0;
}

void set_cpu_id(CPUID id)
{
    cpu_id = id;
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

#include "../../sys/thread_local_arch.h"

Address ThreadLocalArch__pointer(void)
{
    Address pointer;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(pointer));
    return pointer;
}

void ThreadLocalArch__set_pointer(Address pointer)
{
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(pointer) : "memory");
}

size_t ThreadLocalArch__control_prefix_size(void)
{
    return 0;
}

#include "../../sys/thread_local_arch.h"

Address ThreadLocalArch__pointer(void)
{
    Address pointer;
    __asm__ volatile("mv %0, tp" : "=r"(pointer));
    return pointer;
}

void ThreadLocalArch__set_pointer(Address pointer)
{
    __asm__ volatile("mv tp, %0" : : "r"(pointer) : "memory");
}

size_t ThreadLocalArch__control_prefix_size(void)
{
    return 16;
}

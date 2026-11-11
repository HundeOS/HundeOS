#include "../../sys/thread_local_arch.h"

static _Thread_local Address current_pointer;

Address ThreadLocalArch__pointer(void)
{
    return current_pointer;
}

void ThreadLocalArch__set_pointer(Address pointer)
{
    current_pointer = pointer;
}

size_t ThreadLocalArch__control_prefix_size(void)
{
    return 0;
}

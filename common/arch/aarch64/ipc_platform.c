#include "ipc/ipc_platform.h"

void IpcPlatform__signal_event(void)
{
    __asm__ __volatile__("sev" ::: "memory");
}

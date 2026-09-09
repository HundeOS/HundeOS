#include "services/service_platform.h"

void ServicePlatform__signal_event(void)
{
    __asm__ __volatile__("sev" ::: "memory");
}

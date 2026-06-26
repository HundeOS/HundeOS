#ifndef __USER_SYS_SERVICE_H
#define __USER_SYS_SERVICE_H

#include <stdbool.h>

#include "syscalls/service.h"

ServiceCode service_wait(ServiceEventPtr event);
ServiceCode service_map_mmio(ServiceMappedRangePtr range);
ServiceCode service_subscribe_irq(uint64_t irq);

static inline bool service_alive(ServiceCode wait_status)
{
    return wait_status != SERVICE_COMPLETE && wait_status != SERVICE_ABORT;
}

static inline bool service_ok(ServiceCode wait_status)
{
    return wait_status == SERVICE_OK;
}

#endif

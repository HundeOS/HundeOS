#include "service.h"

#include "syscall.h"

ServiceCode service_wait(ServiceEventPtr event)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_WAIT, (Anything) event, 0, 0, 0);
}

ServiceCode service_map_mmio(ServiceMappedRangePtr range)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_MAP_MMIO, (Anything) range, 0, 0, 0);
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0);
}

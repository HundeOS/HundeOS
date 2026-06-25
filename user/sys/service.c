#include "service.h"

#include "syscall.h"

ServiceCode service_wait(ServiceEventPtr event)
{
    return (ServiceCode) syscall(SERVICE_CALL, SERVICE_WAIT, (Anything) event, 0, 0, 0);
}

#ifndef __USER_SYS_SERVICE_H
#define __USER_SYS_SERVICE_H

#include "syscalls/service.h"

ServiceCode service_wait(ServiceEventPtr event);

#endif

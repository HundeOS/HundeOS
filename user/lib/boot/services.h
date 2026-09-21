#ifndef HUND_BOOT_SERVICES_H
#define HUND_BOOT_SERVICES_H
#include "sys/spawn.h"
#include "syscalls/service.h"
ResultDef(ServiceCode);
Result(ServiceCode)
    BootService__start(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready);
Result(ServiceCode)
    BootService__bootstrap(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready);
#endif

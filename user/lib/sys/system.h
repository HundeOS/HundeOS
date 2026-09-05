#ifndef __USER_SYS_SYSTEM_H
#define __USER_SYS_SYSTEM_H

#include "syscalls/calls.h"

SystemCallResult shutdown_system(void);
SystemCallResult panic_system(void);

#endif

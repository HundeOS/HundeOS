#ifndef __USER_SYS_PROCESS_SLEEP_H
#define __USER_SYS_PROCESS_SLEEP_H

#include "syscalls/process.h"

ProcessSleepResult sleep_usec(uint64_t usec);

#endif

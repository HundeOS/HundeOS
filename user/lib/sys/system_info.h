#ifndef HUND_USER_SYS_SYSTEM_INFO_H
#define HUND_USER_SYS_SYSTEM_INFO_H

#include <stddef.h>
#include <stdint.h>

#include "syscalls/system_info.h"

Result(SystemSettings) SystemSettings__load(void);
Result(KernelTraceSize) KernelTrace__load(uint8_t * destination, size_t capacity);

#endif

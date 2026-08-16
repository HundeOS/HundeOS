#ifndef __KERNEL_CPU_H
#define __KERNEL_CPU_H
#include <stdint.h>

#include "arch/cpu.h"
#include "system_info.h"


uint64_t CPU__count();
void CPU__enter_kernel(SystemInfoPtr boot_system_info);



#endif

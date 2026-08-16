#ifndef __KERNEL_SMP_MAIN_H
#define __KERNEL_SMP_MAIN_H

#include "system_info.h"

void Kernel__boot(void);
void Kernel_main(uint64_t cpu_id);

#endif

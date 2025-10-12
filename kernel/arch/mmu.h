#ifndef __KERNEL_ARCH_MMU_H_
#define __KERNEL_ARCH_MMU_H_

#include "../address.h"

void MMU__init();
void MMU__install_null_low_mem_table();

void MMU__install_low_mem_table(KernelAddress table);


#endif

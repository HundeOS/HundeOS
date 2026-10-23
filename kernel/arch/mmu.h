#ifndef __KERNEL_ARCH_MMU_H_
#define __KERNEL_ARCH_MMU_H_

#include "../address.h"

void MMU__init();
typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

void MMU__install_null_low_mem_table(KernelPtr kernel);

void MMU__dereference_user_mappings(KernelPtr kernel, KernelAddress table);

void MMU__install_low_mem_table(KernelAddress table);

#endif

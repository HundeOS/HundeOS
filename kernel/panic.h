#ifndef __KERNEL_HALT_H
#define __KERNEL_HALT_H

#include "arch/panic.h"

void Kernel__panic(const char * error, ProcessContextPtr ctx);

#endif

#ifndef __KERNEL_ARCH_HALT_H
#define __KERNEL_ARCH_HALT_H

#include "process.h"

extern void Kernel__halt();
extern void Kernel__halt_all();
extern void Kernel__dump_state(ThreadContextPtr ctx);
extern void Kernel__dump_context(ThreadContextPtr ctx);
#endif

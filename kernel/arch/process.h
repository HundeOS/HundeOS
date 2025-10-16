#ifndef __KERNEL_ARCH_PROCESS_H
#define __KERNEL_ARCH_PROCESS_H

#include <stdint.h>

typedef struct ProcessContext ProcessContext;
typedef struct Process Process;

extern uint64_t Process__start(Process * self);

#endif
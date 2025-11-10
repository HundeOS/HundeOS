#ifndef __KERNEL_ARCH_PROCESS_H
#define __KERNEL_ARCH_PROCESS_H

#include <stdint.h>
#include "process_context.h"

typedef uint64_t ProcessID;

typedef struct Process Process;
typedef Process * ProcessPtr;

void Process__context_switch(ProcessPtr self);
void Process__save_context(ProcessPtr self, ProcessContextPtr context);

#endif
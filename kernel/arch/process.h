#ifndef __KERNEL_ARCH_PROCESS_H
#define __KERNEL_ARCH_PROCESS_H

#include <stdint.h>
#include "../stack.h"

typedef struct ProcessContext ProcessContext;
typedef ProcessContext * ProcessContextPtr;

typedef struct Process Process;
typedef Process * ProcessPtr;

extern uint64_t Process__start(ProcessPtr self);

extern ProcessContextPtr ProcessContext__init(StackPtr stack, UserAddress entry_point, KernelAddress page_table);

#endif
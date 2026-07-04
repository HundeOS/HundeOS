#ifndef _KERNEL_ARCH_PROCESS_CONTEXT_H
#define _KERNEL_ARCH_PROCESS_CONTEXT_H

#include <stdint.h>

#include "../address.h"
#include "../stack.h"

typedef uint64_t ProcessID;

typedef struct ProcessContext ProcessContext;
typedef ProcessContext * ProcessContextPtr;

ProcessContextPtr ProcessContext__init(ProcessID pid,
                                       StackPtr stack,
                                       UserAddress entry_point,
                                       KernelAddress page_table,
                                       uint64_t argc,
                                       Address argv);
void ProcessContext__assert_user_irqs_unmasked(ProcessContextPtr self);

#endif

#ifndef _KERNEL_ARCH_PROCESS_CONTEXT_H
#define _KERNEL_ARCH_PROCESS_CONTEXT_H

#include <stdint.h>

#include "../address.h"
#include "../stack.h"
#include "../types/optional.h"
#include "../types/result.h"

typedef uint64_t ProcessID;

typedef struct ProcessContext ProcessContext;
typedef ProcessContext * ProcessContextPtr;

OptionalDef(ProcessContextPtr);

typedef enum ProcessContextSaveCode
{
    PROCESS_CONTEXT_SAVE_OK = 0,
    PROCESS_CONTEXT_SAVE_DESTINATION_MISSING = -1,
    PROCESS_CONTEXT_SAVE_INVALID_STATE = -2,
} ProcessContextSaveCode;

ResultDef(ProcessContextSaveCode);

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

ProcessContextPtr ProcessContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                       KernelAddress page_table, uint64_t argc, Address argv);
void ProcessContext__copy(ProcessContextPtr self, ProcessContextPtr source);
void ProcessContext__dispose(ProcessContextPtr self);
void ProcessContext__assert_user_irqs_unmasked(ProcessContextPtr self);

#endif

#ifndef _KERNEL_ARCH_PROCESS_CONTEXT_H
#define _KERNEL_ARCH_PROCESS_CONTEXT_H

#include <stdint.h>

#include "../address.h"
#include "../stack.h"
#include "../types/optional.h"
#include "../types/result.h"
#include "../../common_includes/syscalls/process.h"
#include "../../common_includes/syscalls/types.h"

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

typedef struct ProcessStartup
{
    uint64_t argument_count;
    Address arguments;
    Address environment;
    Address current_directory;
    Address exec_payload;
} ProcessStartup;

typedef const ProcessStartup * ConstProcessStartupPtr;

static inline ProcessStartup ProcessStartup__new(uint64_t argument_count, Address arguments, Address environment,
                                                 Address current_directory, Address exec_payload)
{
    return (ProcessStartup){
        .argument_count = argument_count,
        .arguments = arguments,
        .environment = environment,
        .current_directory = current_directory,
        .exec_payload = exec_payload,
    };
}

ProcessContextPtr ProcessContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                       KernelAddress page_table, ConstProcessStartupPtr startup);
ProcessContextPtr ProcessContext__clone(KernelPtr kernel, ProcessContextPtr source);
void ProcessContext__copy(ProcessContextPtr self, ProcessContextPtr source);
void ProcessContext__set_syscall_result(ProcessContextPtr self, SystemCallResult result);
void ProcessContext__restart_syscall(ProcessContextPtr self);
void ProcessContext__dispose(ProcessContextPtr self);
void ProcessContext__assert_user_irqs_unmasked(ProcessContextPtr self);
void ProcessContext__enter_signal(ProcessContextPtr self, ProcessSignal signal, Address handler, Address restorer);

#endif

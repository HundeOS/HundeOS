#ifndef _KERNEL_ARCH_THREAD_CONTEXT_H
#define _KERNEL_ARCH_THREAD_CONTEXT_H

#include <stdint.h>

#include "../address.h"
#include "../stack.h"
#include "../types/optional.h"
#include "../types/result.h"
#include "../../common_includes/syscalls/process.h"
#include "../../common_includes/syscalls/types.h"

typedef uint64_t ProcessID;

typedef struct ThreadContext ThreadContext;
typedef ThreadContext * ThreadContextPtr;

OptionalDef(ThreadContextPtr);

typedef enum ThreadContextSaveCode
{
    THREAD_CONTEXT_SAVE_OK = 0,
    THREAD_CONTEXT_SAVE_DESTINATION_MISSING = -1,
    THREAD_CONTEXT_SAVE_INVALID_STATE = -2,
} ThreadContextSaveCode;

ResultDef(ThreadContextSaveCode);

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

ThreadContextPtr ThreadContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                     KernelAddress page_table, ConstProcessStartupPtr startup);
ThreadContextPtr ThreadContext__init_thread(KernelPtr kernel, StackPtr stack, UserAddress entry_point,
                                            Address start_function, Anything argument);
ThreadContextPtr ThreadContext__clone(KernelPtr kernel, ThreadContextPtr source);
void ThreadContext__copy(ThreadContextPtr self, ThreadContextPtr source);
void ThreadContext__set_syscall_result(ThreadContextPtr self, SystemCallResult result);
void ThreadContext__set_thread_pointer(ThreadContextPtr self, Address thread_pointer);
Address ThreadContext__thread_pointer(ThreadContextPtr self);
void ThreadContext__restart_syscall(ThreadContextPtr self);
void ThreadContext__dispose(ThreadContextPtr self);
void ThreadContext__assert_user_irqs_unmasked(ThreadContextPtr self);
void ThreadContext__enter_signal(ThreadContextPtr self, ProcessSignal signal, Address handler, Address restorer);

#endif

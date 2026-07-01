#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

#include <stdint.h>

#include "bin/elf.h"
#include "all_defs.h"
#include "stack.h"
#include "types/comparison.h"
#include "types/fixed_array.h"
#include "types/kstring_additional_types.h"
#include "types/linked_list.h"
#include "syscalls/io.h"
#include "syscalls/spawn.h"
#include "process_class.h"
#include "service/service.h"

typedef struct ProcessContext ProcessContext;
typedef struct ProcessContext * ProcessContextPtr;

typedef struct ParkedKernelContext ParkedKernelContext;
typedef ParkedKernelContext * ParkedKernelContextPtr;

OptionalDef(ParkedKernelContextPtr);
ResultDef(ParkedKernelContextPtr);
LinkedListDefine(ParkedKernelContextPtr, ParkedKernelContexts, &Ptr__equals);

typedef uint64_t ProcessID;
typedef uint64_t ProcessState;
typedef uint64_t Tick;

OptionalDef(ProcessID);
ResultDef(ProcessID);

// state bits
// 6    5      4     3    2         1     0
// dead stop active sleep wait interrupt ready
//
typedef enum ProcessStates{
    PROC_STATE_NEW               = 0b0000000, // Being created
    PROC_STATE_READY             = 0b0000001, // Ready to run
    PROC_STATE_RUNNING           = 0b0010000, // Currently executing
    PROC_STATE_INTERRUPTED       = 0b0000010, // Interrupted by signal
    PROC_STATE_WAITING           = 0b0000100, // Waiting for I/O or event
    PROC_STATE_SLEEPING          = 0b0001000, // Sleeping (timed wait)
    PROC_STATE_STOPPED           = 0b0100000, // Stopped (e.g., by signal)
    PROC_STATE_DEAD              = 0b1000000, // Being destroyed
    PROC_STATE_SWITCHING_CONTEXT = 0b0100100, // Switching context
} ProcessStates;

typedef enum ProcessErrors
{
    PROC_ERROR_NO_PARKED = -1,
} ProcessErrors;

typedef uint64_t FileDescriptor;
typedef uint64_t ProcessIOHandle;

#define PROCESS_ABORT_EXIT_BASE (-1000LL)

typedef enum ProcessAbortReason
{
    PROCESS_ABORT_DATA_ABORT = 1,
    PROCESS_ABORT_INSTRUCTION_ABORT = 2,
    PROCESS_ABORT_UNDEFINED_INSTRUCTION = 3,
    PROCESS_ABORT_ALIGNMENT = 4,
    PROCESS_ABORT_BAD_SYSCALL = 5,
    PROCESS_ABORT_UNKNOWN_USER_EXCEPTION = 6,
} ProcessAbortReason;

typedef struct ProcessAbort
{
    ProcessAbortReason reason;
    const char * exception_description;
    const char * fault_status_description;
    const char * access_description;
    Address pc;
    Address fault_address;
    uint64_t exception_code;
    uint64_t fault_status_code;
    uint64_t syndrome;
    uint64_t cpu;
} ProcessAbort;

OptionalDef(ProcessAbort);

ResultDef(FileDescriptor);
ResultDef(ProcessIOHandle);
OptionalDef(FileDescriptor);
OptionalDef(ProcessIOHandle);

FixedArrayDefine(KString, SPAWN_MAX_ARGS + 1, ProcessArguments, kstring_equals);

inline const char * ProcessState__to_string(ProcessState state)
{
    switch (state)
    {
        case PROC_STATE_NEW:
            return "PROC_STATE_NEW";
        case PROC_STATE_READY:
            return "PROC_STATE_READY";
        case PROC_STATE_RUNNING:
            return "PROC_STATE_RUNNING";
        case PROC_STATE_INTERRUPTED:
            return "PROC_STATE_INTERRUPTED";
        case PROC_STATE_WAITING:
            return "PROC_STATE_WAITING";
        case PROC_STATE_SLEEPING:
            return "PROC_STATE_SLEEPING";
        case PROC_STATE_STOPPED:
            return "PROC_STATE_STOPPED";
        case PROC_STATE_DEAD:
            return "PROC_STATE_DEAD";
        case PROC_STATE_SWITCHING_CONTEXT:
            return "PROC_STATE_SWITCHING_CONTEXT";
        default:
            return "UNKNOWN";
    }
}

typedef enum ProcessSetupStatus
{
    PROCESS_FAILED_TO_SETUP = -1,
    PROCESS_SETUP = 0,
} ProcessSetupStatus;

ResultDef(ProcessSetupStatus);

typedef enum ProcessMemoryCode
{
    PROCESS_MEMORY_OK = 0,
    PROCESS_MEMORY_NOT_FOUND = -1,
    PROCESS_MEMORY_FULL = -2,
    PROCESS_MEMORY_STATIC_ALLOCATION = -3,
    PROCESS_MEMORY_FREE_FAILED = -4,
    PROCESS_MEMORY_INVALID_SIZE = -5,
} ProcessMemoryCode;

ResultDef(ProcessMemoryCode);

typedef enum UserAllocationType
{
    USER_ALLOCATION_ELF,
    USER_ALLOCATION_STACK,
    USER_ALLOCATION_HEAP,
    USER_ALLOCATION_DYNAMIC,
    USER_ALLOCATION_MMIO,
} UserAllocationType;

typedef struct UserAllocation
{
    UserAddress address;
    size_t size;
    UserAllocationType type;
} UserAllocation;

typedef UserAllocation * UserAllocationPtr;

OptionalDef(UserAllocation);
bool UserAllocation__equals(UserAllocationPtr self, UserAllocationPtr other);

LinkedListDefine(UserAllocation, UserAllocations, (EqualFunction) UserAllocation__equals);
typedef struct ProcessFileDescriptor
{
    FileDescriptor fd;
    ProcessIOHandle handle;
} ProcessFileDescriptor;

typedef ProcessFileDescriptor * ProcessFileDescriptorPtr;

OptionalDef(ProcessFileDescriptor);
bool ProcessFileDescriptor__equals(ProcessFileDescriptorPtr self, ProcessFileDescriptorPtr other);

LinkedListDefine(
    ProcessFileDescriptor, ProcessFileDescriptors, (EqualFunction) ProcessFileDescriptor__equals
);

typedef struct Process Process;
typedef Process * ProcessPtr;

OptionalDef(ProcessPtr);
ResultDef(ProcessPtr);

typedef struct Process {
    KernelAddress page_table;

    ProcessContextPtr context;
    Stack stack;
    Optional(ProcessPtr) parent;
    ProcessID pid;
    ProcessState state;
    UserAddress entry_point;
    Tick last_tick;
    KString name;
    Address heap_break;
    Address heap_mapped_end;
    Address allocation_next;
    UserAllocations user_allocations;
    ProcessFileDescriptors file_descriptors;
    FileDescriptor next_file_descriptor;
    Optional(ProcessAbort) abort;
    ProcessClass process_class;
    ServiceEvents service_events;
    ServiceEventPtr service_wait_target;

    ParkedKernelContexts parked_contexts;
} Process;

Result(ProcessPtr) Process__new(
    KString name,
    KernelAddress page_table,
    UserAddress entry,
    Stack stack,
    Optional(ProcessPtr) parent,
    ProcessArguments arguments
);

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessTable, &Ptr__equals);

OptionalType(ProcessTable *, ProcessTablePtr);

Result(ProcessPtr) Process__setup(Elf binary, Optional(ProcessPtr) parent);
Result(ProcessPtr) Process__setup_with_args(Elf binary, Optional(ProcessPtr) parent, ProcessArguments arguments);
Result(CollectionCode) Process__push_parked_context(ProcessPtr self, ParkedKernelContextPtr context);
Optional(ParkedKernelContextPtr) Process__top_parked_context(ProcessPtr self);
Optional(ParkedKernelContextPtr) Process__pop_parked_context(ProcessPtr self);
bool Process__is_parked(ProcessPtr self);
Result(ProcessMemoryCode) Process__record_user_allocation(
    ProcessPtr self, UserAddress address, size_t size, UserAllocationType type
);
Result(Address) Process__device_address(ProcessPtr self, Address virtual_address);
Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size);
Result(UserAddress) Process__map_mmio(ProcessPtr self, Address physical, size_t size);
Result(Address) Process__sbrk(ProcessPtr self, int64_t increment);
Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address);
Result(ProcessMemoryCode) Process__dispose_user_allocations(ProcessPtr self);
Result(FileDescriptor) Process__allocate_file_descriptor(ProcessPtr self, ProcessIOHandle handle);
Result(FileDescriptor) Process__set_file_descriptor(ProcessPtr self, FileDescriptor fd, ProcessIOHandle handle);
Result(ProcessIOHandle) Process__get_io_handle(ProcessPtr self, FileDescriptor fd);
Result(ProcessIOHandle) Process__remove_file_descriptor(ProcessPtr self, FileDescriptor fd);

#endif

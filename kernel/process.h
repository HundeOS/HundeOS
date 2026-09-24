#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

#include <stdint.h>

#include "bin/elf.h"
#include "all_defs.h"
#include "stack.h"
#include "types/comparison.h"
#include "types/fixed_array.h"
#include "types/bstring_additional_types.h"
#include "types/linked_list.h"
#include "mutex.h"
#include "syscalls/io.h"
#include "../common_includes/syscalls/process.h"
#include "syscalls/spawn.h"
#include "process_class.h"
#include "service/service.h"
#include "scheduler/types.h"
#include "scheduler/cpu_usage.h"
#include "scheduler/queue_wait_usage.h"
#include "arch/exception.h"
#include "arch/process_context.h"

OptionalDef(UserAddress);

typedef struct ParkedOperation ParkedOperation;
typedef ParkedOperation * ParkedOperationPtr;

OptionalDef(ParkedOperationPtr);
ResultDef(ParkedOperationPtr);
LinkedListDefine(ParkedOperationPtr, ParkedOperations, &Ptr__equals);

typedef uint64_t ProcessID;
typedef ProcessID * ProcessIDPtr;
#include "../common/process/process_types.h"
typedef uint64_t Tick;

OptionalDef(ProcessID);
ResultDef(ProcessID);
OptionalDef(ProcessSignal);
OptionalDef(ProcessChildEvent);
ResultDef(ProcessChildEvent);
OptionalDef(UserID);
OptionalDef(GroupID);

typedef enum ProcessErrors
{
    PROC_ERROR_NO_PARKED = -1,
} ProcessErrors;

#define PROCESS_ABORT_EXIT_BASE (-1000LL)
#define PROCESS_SIGNAL_EXIT_BASE (-2000LL)

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
    ExceptionSyndrome syndrome;
    uint64_t cpu;
} ProcessAbort;

OptionalDef(ProcessAbort);

ResultDef(SpawnFileDescriptor);
ResultDef(SpawnStdio);

FixedArrayDefine(BString, SPAWN_MAX_ARGS + 1, ProcessArguments, bstring_equals);
typedef ProcessArguments * ProcessArgumentsPtr;
typedef const ProcessArguments * ConstProcessArgumentsPtr;
bool ProcessChildEvent__equals(ProcessChildEventPtr self, ProcessChildEventPtr other);
#define MAX_PROCESS_CHILD_EVENTS 64
FixedArrayDefine(ProcessChildEvent, MAX_PROCESS_CHILD_EVENTS, ProcessChildEvents,
                 (EqualFunction)ProcessChildEvent__equals);

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
    USER_ALLOCATION_SHARED,
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

typedef struct SharedUserPages
{
    UserAddress allocation;
    UserAddress mapping;
} SharedUserPages;

ResultDef(SharedUserPages);

static inline SharedUserPages SharedUserPages__new(UserAddress allocation, UserAddress mapping)
{
    return (SharedUserPages){
        .allocation = allocation,
        .mapping = mapping,
    };
}

LinkedListDefine(UserAllocation, UserAllocations, (EqualFunction)UserAllocation__equals);

typedef struct Process Process;
typedef struct ExecLoad ExecLoad;
typedef ExecLoad * ExecLoadPtr;
OptionalDef(ExecLoadPtr);
typedef Process * ProcessPtr;
typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

OptionalDef(ProcessPtr);
ResultDef(ProcessPtr);

typedef struct Process
{
    mutex_t lock;
    KernelPtr kernel;
    ProcessPageTable page_table;

    Optional(ProcessContextPtr) context;
    Stack stack;
    Optional(ProcessPtr) parent;
    ProcessID pid;
    ProcessState state;
    UserAddress entry_point;
    Tick last_tick;
    uint64_t dispatched_at;
    bool migration_safe;
    bool preferred_active;
    bool preferred_warm;
    Optional(RequestTicket) preferred_ticket;
    CPUUsage cpu_usage;
    QueueWaitUsage queue_wait_usage;
    Optional(UserAddress) queue_wait_control;
    BString name;
    Address heap_break;
    Address heap_mapped_end;
    Address allocation_next;
    UserAllocations user_allocations;
    Optional(UserAddress) virtio_device;
    SpawnStdio startup_stdio;
    Optional(ProcessAbort) abort;
    ProcessClass process_class;
    Optional(ExecLoadPtr) exec_load;
    ServiceState service;
    Optional(UserAddress) idle_debug_address;
    UserID user_id;
    GroupID group_id;
    ProcessCapabilities capabilities;
    ProcessSignalMask pending_signals;
    ProcessChildEvents child_events;

    ParkedOperations parked_operations;
} Process;

Result(ProcessPtr)
    Process__new(KernelPtr kernel, ConstBStringPtr name, ConstProcessPageTablePtr page_table, ConstUserAddressPtr entry,
                 ConstStackPtr stack, OptionalPtr(ProcessPtr) parent, ConstProcessArgumentsPtr arguments);

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessTable, &Ptr__equals);

OptionalType(ProcessTable *, ProcessTablePtr);

Result(ProcessPtr) Process__setup(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent);
Result(ProcessPtr) Process__setup_with_args(KernelPtr kernel, ConstElfPtr binary, OptionalPtr(ProcessPtr) parent,
                                            ConstProcessArgumentsPtr arguments);
Result(ProcessContextSaveCode) Process__store_context(ProcessPtr self, ProcessContextPtr context);
Result(ProcessContextSaveCode) Process__save_context(ProcessPtr self, const OptionalPtr(ProcessContextPtr) context);
ProcessContextPtr Process__required_context(ProcessPtr self);
void Process__activate_address_space(ProcessPtr self);
void Process__context_switch(ProcessPtr self);
Result(ProcessSetupStatus) Process__dispose(ProcessPtr self);
Result(CollectionCode) Process__push_parked_operation(ProcessPtr self, ParkedOperationPtr operation);
Optional(ParkedOperationPtr) Process__pop_parked_operation(ProcessPtr self);
Optional(ParkedOperationPtr) Process__pop_ready_parked_operation(ProcessPtr self);
Optional(ParkReason) Process__top_park_reason(ProcessPtr self);
Optional(SchedulerPtr) Process__top_parked_operation_scheduler(ProcessPtr self);
bool Process__has_parked_operation(ProcessPtr self, ParkReason reason, OptionalPtr(OperationKey) operation_key);
bool Process__has_ready_parked_operation(ProcessPtr self, ParkReason reason, OptionalPtr(OperationKey) operation_key);
bool Process__mark_parked_operation_ready(ProcessPtr self, ParkReason reason, OptionalPtr(OperationKey) operation_key,
                                          OptionalPtr(ReturnCode) return_code);
bool Process__wake_expired_sleep(ProcessPtr self, Tick current_tick);
bool Process__is_parked(ProcessPtr self);
bool Process__is_handling_service_irq(ProcessPtr self);
void Process__set_identity(ProcessPtr self, UserID user_id, GroupID group_id, ProcessCapabilities capabilities);
void Process__set_root_identity(ProcessPtr self);
void Process__set_hund_identity(ProcessPtr self);
void Process__set_user_identity(ProcessPtr self);
bool Process__is_root(ProcessPtr self);
bool Process__has_capability(ProcessPtr self, ProcessCapabilities capability);
bool Process__supports_signal(ProcessPtr self, ProcessSignal signal);
void Process__queue_signal(ProcessPtr self, ProcessSignal signal);
bool Process__has_pending_signal(ProcessPtr self);
Optional(ProcessSignal) Process__take_pending_signal(ProcessPtr self);
ReturnCode ProcessSignal__exit_code(ProcessSignal signal);
Result(ProcessChildEvent) Process__pop_next_child_event(ProcessPtr self);
Result(CollectionCode) Process__push_child_event(ProcessPtr self, ConstProcessChildEventPtr event);
Result(ProcessSetupStatus) Process__dispose_owned_kernel_resources(ProcessPtr self);
Result(KAllocCode) Process__dispose_page_table(ProcessPtr self);
Result(ProcessMemoryCode)
    Process__record_user_allocation(ProcessPtr self, UserAddress address, size_t size, UserAllocationType type);
Result(Address) Process__device_address(ProcessPtr self, Address virtual_address);
Result(ProcessMemoryCode) Process__claim_virtio_device(ProcessPtr self, Address mapping);
void Process__quiesce_device(ProcessPtr self);
Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size);
Result(Address) Process__queue_wait_control(ProcessPtr self);
void Process__wake_shared_wait(ProcessPtr self);
Result(UserAddress)
    Process__map_shared_user_pages(ProcessPtr self, ProcessPtr source, Address source_virtual_address, size_t size);
Result(SharedUserPages) Process__map_shared_user_pages_with_allocation(ProcessPtr self, ProcessPtr source,
                                                                       Address source_virtual_address, size_t size);
Result(UserAddress) Process__map_idle_debug_page(ProcessPtr self);
Result(UserAddress) Process__map_mmio(ProcessPtr self, Address physical, size_t size);
Result(Address) Process__sbrk(ProcessPtr self, int64_t increment);
Result(ProcessMemoryCode) Process__free_user_pages(ProcessPtr self, Address virtual_address);
Result(ProcessMemoryCode) Process__dispose_user_allocations(ProcessPtr self);
Result(SpawnStdio) Process__stdio_for_spawn(ProcessPtr self, ConstSpawnStdioPtr requested);
void Process__prepare_startup_stdio(ProcessPtr self);

#endif

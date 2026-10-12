#ifndef __KERNEL_SCHEDULER_SCHEDULER_H
#define __KERNEL_SCHEDULER_SCHEDULER_H

#include <stdint.h>

#include "../types/result.h"
#include "../types/fixed_array.h"
#include "../types/map.h"
#include "../types/hashtable.h"
#include "types.h"
#include "kernel_context.h"
#include "parked.h"
#include "context_switch.h"
#include "../process.h"
#include "../system_info.h"
#include "../service/event_dispatcher.h"
#include "../ipc/queues.h"
#include "../ipc/buffers.h"
#include "request_graph.h"
#include "syscalls/service.h"
#include "syscalls/mem.h"

typedef uint64_t SchedulerInitStatus;
typedef uint64_t SchedulerScheduleStatus;

ResultDef(SchedulerInitStatus);
ResultDef(SchedulerScheduleStatus);
ResultDef(ProcessGroupID);
ResultDef(ProcessSessionID);
OptionalDef(CPUID);

typedef enum SchedulerInitStatuses
{
    SCHEDULER_INIT_OK = 0,
    SCHEDULER_INIT_ERROR = -1,
} SchedulerInitStatuses;

typedef enum SchedulerScheduleStatuses
{
    SCHEDULER_SCHEDULE_OK = 0,
} SchedulerScheduleStatuses;

typedef enum SchedulerErrors
{
    SCHEDULER_INVALID_CPU = -1,
    SCHEDULER_GENERAL_ERROR = -2,
    SCHEDULER_NOT_STARTED = -3,
    SCHEDULER_PROCESS_NOT_FOUND = -4,
    SCHEDULER_CANT_UNPARK = -5,
} SchedulerErrors;

typedef struct SchedulerWake
{
    bool reschedule;
    CPUID cpu;
} SchedulerWake;

static inline SchedulerWake SchedulerWake__new(bool reschedule, CPUID cpu)
{
    return (SchedulerWake){.reschedule = reschedule, .cpu = cpu};
}

ResultDef(SchedulerWake);

typedef struct CPUScheduler
{
    ProcessTable processes;
    ProcessPtr idle_process;
    mutex_t scheduler_lock;
    Tick last_tick;
    CPUID cpu;
    uint64_t registry_slot;
    bool started;
    uint64_t last_registry_sample_usec;
    Optional(ProcessPtr) switching_out;
} CPUScheduler;

typedef CPUScheduler * CPUSchedulerPtr;

OptionalDef(CPUSchedulerPtr);
ResultDef(CPUSchedulerPtr);
OptionalDef(KernelPtr);

HashtableDefine(CPUID, CPUSchedulerPtr, MAX_CPUS, MAX_CPUS, CPUSchedulers, &hash__uint64, SimpleEquals(uint64_t),
                &Ptr__equals);

typedef struct ManagedSharedPage
{
    UserAddress address;
    ProcessID owner_pid;
    ProcessID first_peer_pid;
    ProcessID second_peer_pid;
    uint64_t generation;
} ManagedSharedPage;

typedef ManagedSharedPage * ManagedSharedPagePtr;
OptionalDef(ManagedSharedPage);
bool ManagedSharedPage__equals(void * first, void * second);
FixedArrayDefine(ManagedSharedPage, MEM_SHARED_PAGE_COUNT, ManagedSharedPages, &ManagedSharedPage__equals);
ResultDef(MemStatus);

typedef struct Scheduler
{
    Optional(KernelPtr) kernel;
    Optional(InterruptControllerInfo) interrupt_controller;
    CPUSchedulers schedulers;
    Address idle_program_start;
    Address idle_program_end;
    BString idle_program_name;
    ProcessTable admission;
    ServiceEventDispatcher service_event_dispatcher;
    ManagedSharedPages shared_pages;
    uint64_t shared_page_generation;
    Queues queues;
    Buffers buffers;
    ProcessTable preferred;
    RequestGraph request_graph;
    mutex_t scheduler_lock;
    bool ready;
} Scheduler;

SchedulerPtr Scheduler__heap_new(void);

Result(SchedulerInitStatus) Scheduler__init(KernelPtr kernel, SchedulerPtr self, Elf idle_process);
Result(SchedulerInitStatus) Scheduler__register_cpu(SchedulerPtr self, CPUID cpu);
void Scheduler__preempt(SchedulerPtr self, CPUID cpu, const OptionalPtr(ProcessContextPtr) context);
void Scheduler__yield_waiting(SchedulerPtr self, ProcessPtr waiting_proc, CPUID cpu);
Result(ProcessID) Scheduler__exit(SchedulerPtr scheduler, ProcessPtr proc, ReturnCode return_code);
Result(ProcessID) Scheduler__abort_process(SchedulerPtr scheduler, ProcessPtr proc, ProcessAbort abort);

Result(ProcessPtr) Scheduler__park(SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                                   OptionalPtr(Continuation) kernel_continuation, ParkReason reason);
Result(ProcessPtr)
    Scheduler__park_with_context_ownership(SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                                           OptionalPtr(Continuation) kernel_continuation, ParkReason reason,
                                           bool owns_kernel_context);
Result(ProcessPtr) Scheduler__park_wait_child(SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                                              OptionalPtr(Continuation) kernel_continuation,
                                              OptionalPtr(KernelAddress) abort_report, bool owns_kernel_context);

Result(ProcessID) Scheduler__park_by_pid(SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
                                         OptionalPtr(Continuation) kernel_continuation, ParkReason reason);
Result(ProcessID)
    Scheduler__park_by_pid_with_context_ownership(SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
                                                  OptionalPtr(Continuation) kernel_continuation, ParkReason reason,
                                                  bool owns_kernel_context);
Result(ProcessID) Scheduler__park_by_pid_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context, OptionalPtr(Continuation) kernel_continuation,
    ParkReason reason, bool owns_kernel_context, OptionalPtr(OperationKey) operation_key);

Result(ProcessPtr)
    Scheduler__unpark(SchedulerPtr self, ProcessPtr proc, ParkReason reason, OptionalPtr(ReturnCode) return_code);
Result(ProcessPtr) Scheduler__sleep_usec(SchedulerPtr self, ProcessPtr process, uint64_t usec);
Result(ProcessPtr) Scheduler__signal_suspend(SchedulerPtr self, ProcessPtr process, ProcessSignalMask mask);

Result(ProcessID)
    Scheduler__unpark_by_pid(SchedulerPtr self, ProcessID pid, ParkReason reason, OptionalPtr(ReturnCode) return_code);
Result(ProcessID) Scheduler__unpark_by_pid_with_operation_key(SchedulerPtr self, ProcessID pid, ParkReason reason,
                                                              OptionalPtr(ReturnCode) return_code,
                                                              OptionalPtr(OperationKey) operation_key);

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu);
Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu);
bool Scheduler__should_preempt_on_timer(SchedulerPtr self, CPUID cpu);
Optional(ProcessPtr) Scheduler__find_process_by_pid(SchedulerPtr self, ProcessID pid);
Optional(ProcessPtr) Scheduler__find_process_by_pid_locked(SchedulerPtr self, ProcessID pid);
Result(ProcessCredentials) Scheduler__process_credentials(SchedulerPtr self, ProcessID pid);
Result(ProcessGroupID) Scheduler__process_group(SchedulerPtr self, ProcessPtr caller, int64_t pid);
Result(ProcessGroupID)
    Scheduler__set_process_group(SchedulerPtr self, ProcessPtr caller, int64_t pid, int64_t process_group_id);
Result(ProcessSessionID) Scheduler__create_session(SchedulerPtr self, ProcessPtr caller);
ProcessTimes Scheduler__process_times(SchedulerPtr self, ProcessPtr process);
Result(ServiceCode)
    Scheduler__require_process_capability(SchedulerPtr self, ProcessID pid, ProcessCapabilities capability);
Result(ProcessID) Scheduler__queue_signal(SchedulerPtr self, ProcessID pid, ProcessSignal signal);
Result(ProcessID) Scheduler__send_signal(SchedulerPtr self, ProcessPtr sender, int64_t target, ProcessSignal signal);
ProcessSignalDeliveryKind Scheduler__deliver_pending_signal(SchedulerPtr self, ProcessPtr process,
                                                            ProcessContextPtr context);
Result(Address)
    Scheduler__alloc_shared_page(SchedulerPtr self, ProcessPtr owner, ProcessID first_peer, ProcessID second_peer);
Result(MemStatus) Scheduler__release_shared_page(SchedulerPtr self, ProcessPtr owner, Address address);
Result(Address) Scheduler__queue_listen(SchedulerPtr self, ProcessPtr process, Address name, size_t length);
Result(Address) Scheduler__queue_operation(SchedulerPtr self, ProcessPtr process, uint64_t operation, uint64_t id);
Result(SchedulerWake)
    Scheduler__queue_data_available(SchedulerPtr self, ProcessPtr process, QueueConnectionID connection);
Result(SchedulerWake) Scheduler__wake_shared_wait(SchedulerPtr self, ProcessPtr process);
Result(QueueWaitCode)
    Scheduler__queue_wait(SchedulerPtr self, ProcessPtr process, IpcEventGeneration observed, Tick ticks);
ProcessTablePtr Scheduler__all_processes(SchedulerPtr self, CPUID cpu);
ProcessTable Scheduler__all_processes_for_all_cpus(SchedulerPtr self);
ProcessTable Scheduler__process_snapshot(SchedulerPtr self);
ProcessListResult Scheduler__process_info_snapshot(SchedulerPtr self, ProcessInfoPtr entries, uint64_t max_entries);

uint64_t Scheduler__process_count(SchedulerPtr self);

Optional(ProcessPtr)
    Scheduler__save_context(SchedulerPtr self, CPUID cpu, const OptionalPtr(ProcessContextPtr) context);

Result(SchedulerScheduleStatus) Scheduler__schedule(SchedulerPtr self, ProcessPtr new_proc);
Result(CPUID) Scheduler__schedule_on_cpu(SchedulerPtr self, ProcessPtr new_proc, CPUID cpu);
Result(CPUID) Scheduler__cpu_for_process(SchedulerPtr self, ProcessPtr process);
Result(SchedulerScheduleStatus) Scheduler__promote_realtime(SchedulerPtr self, ProcessPtr process);
Result(SchedulerInitStatus) Scheduler__start(SchedulerPtr self, CPUID cpu);
void Scheduler__mark_ready(SchedulerPtr self);
bool Scheduler__ready(SchedulerPtr self);
Result(SchedulerInitStatus) Scheduler__wait_until_ready(SchedulerPtr self);
Result(ServiceCode) Scheduler__subscribe_irq(SchedulerPtr self, ProcessPtr process, IRQNumber irq);
Result(ServiceCode) Scheduler__send_irq_event(SchedulerPtr self, IRQNumber irq);
Result(ServiceEvent) Scheduler__service_take_next_event(SchedulerPtr self, ProcessPtr process);
Result(ServiceEvent) Scheduler__service_wait(SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context,
                                             OptionalPtr(Continuation) continuation);
Result(ServiceEvent)
    Scheduler__service_wait_with_context_ownership(SchedulerPtr self, ProcessPtr process,
                                                   KernelContextPtr kernel_context,
                                                   OptionalPtr(Continuation) continuation, bool owns_kernel_context);
Result(ProcessChildEvent)
    Scheduler__child_wait_with_context_ownership(SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context,
                                                 OptionalPtr(Continuation) continuation, bool owns_kernel_context,
                                                 int64_t child_selection, uint64_t options);
Result(ProcessChildEvent)
    Scheduler__child_take_event(SchedulerPtr self, ProcessPtr process, int64_t child_selection, uint64_t options);
Result(ServiceCode) Scheduler__service_enqueue(SchedulerPtr self, ProcessPtr process, ConstServiceEventPtr event);
Result(ServiceCode) Scheduler__service_publish_ready(SchedulerPtr self, ProcessPtr process, ConstBStringPtr name);
Optional(ProcessPtr) Scheduler__service_ready_process(SchedulerPtr self, ConstBStringPtr name);
Result(ServiceCode)
    Scheduler__service_wait_ready_with_context_ownership(SchedulerPtr self, ProcessPtr process, ConstBStringPtr name,
                                                         KernelContextPtr kernel_context,
                                                         OptionalPtr(Continuation) continuation,
                                                         bool owns_kernel_context);
Result(CollectionCode) Scheduler__add_blocked_operation(SchedulerPtr self, ParkedOperationPtr operation,
                                                        ParkedOperationPtr blocked_operation);
Result(CollectionCode) Scheduler__resolve_blocked_operation(SchedulerPtr self, ParkedOperationPtr operation,
                                                            ParkedOperationPtr blocked_operation);
bool Scheduler__parked_operation_is_ready(SchedulerPtr self, ParkedOperationPtr operation);

#define NEW_SCHEDULER                                                                                                  \
    (Scheduler)                                                                                                        \
    {                                                                                                                  \
        .kernel = None(KernelPtr), .interrupt_controller = None(InterruptControllerInfo),                              \
        .schedulers = EmptyHashtable(CPUSchedulers), .admission = EmptyArray(ProcessTable),                            \
        .service_event_dispatcher = NEW_SERVICE_EVENT_DISPATCHER, .shared_pages = EmptyArray(ManagedSharedPages),      \
        .queues = QUEUES_INIT, .scheduler_lock = MUTEX_INIT, .ready = false,                                         \
    }

#endif

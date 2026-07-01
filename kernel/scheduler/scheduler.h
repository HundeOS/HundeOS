#ifndef __KERNEL_SCHEDULER_SCHEDULER_H
#define __KERNEL_SCHEDULER_SCHEDULER_H

#include <stdint.h>

#include "../types/result.h"
#include "../types/fixed_array.h"
#include "../types/map.h"
#include "types.h"
#include "kernel_context.h"
#include "parked.h"
#include "context_switch.h"
#include "../process.h"
#include "subsystems/message/message_broker.h"
#include "syscalls/service.h"

typedef uint64_t SchedulerInitStatus;

ResultDef(SchedulerInitStatus);

typedef enum SchedulerInitStatuses
{
    SCHEDULER_INIT_OK = 0,
    SCHEDULER_INIT_ERROR = -1,
} SchedulerInitStatuses;

typedef enum SchedulerErrors
{
    SCHEDULER_INVALID_CPU = -1,
    SCHEDULER_GENERAL_ERROR = -2,
    SCHEDULER_NOT_STARTED = -3,
    SCHEDULER_PROCESS_NOT_FOUND = -4,
    SCHEDULER_CANT_UNPARK = -5,
} SchedulerErrors;

typedef uint64_t IRQNumber;
OptionalDef(IRQNumber);

typedef struct IRQService
{
    ProcessPtr process;
    bool pending;
    bool enabled;
    uint64_t delivery_count;
    uint64_t coalesced_count;
}
IRQService;

typedef IRQService * IRQServicePtr;

OptionalDef(IRQService);
OptionalDef(IRQServicePtr);

typedef struct CPUScheduler
{
    ProcessTable processes;
    ProcessPtr idle_process;
    mutex_t scheduler_lock;
    Tick last_tick;
    CPUID cpu;
    bool started;
} CPUScheduler;

typedef CPUScheduler * CPUSchedulerPtr;

OptionalDef(CPUSchedulerPtr);
ResultDef(CPUSchedulerPtr);

FixedArrayDefine(CPUSchedulerPtr, MAX_CPUS, CPUSchedulers, &Ptr__equals);
MapDefine(IRQNumber, IRQService, 256, IRQServiceMap, SimpleEquals(uint64_t));

typedef struct Scheduler
{
    MessageBrokerPtr message_broker;
    CPUSchedulers schedulers;
    IRQServiceMap irq_services;
    mutex_t scheduler_lock;
    bool ready;
} Scheduler;

typedef struct IOSubsystem IOSubsystem;
typedef IOSubsystem * IOSubsystemPtr;

SchedulerPtr Scheduler__heap_new(MessageBrokerPtr message_broker);

Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, Elf idle_process);
void Scheduler__preempt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context);
void Scheduler__yield_waiting(SchedulerPtr self, ProcessPtr waiting_proc, CPUID cpu);
Result(ProcessPtr) Scheduler__exit(SchedulerPtr scheduler, ProcessPtr proc, ReturnCode return_code);
Result(ProcessPtr) Scheduler__abort_process(
    SchedulerPtr scheduler, IOSubsystemPtr io_subsystem, ProcessPtr proc, ProcessAbort abort
);

Result(ProcessPtr) Scheduler__park(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason
);

Result(ProcessID) Scheduler__park_by_pid(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason
);

Result(ProcessPtr) Scheduler__unpark(SchedulerPtr self, ProcessPtr proc, ParkReason reason, Optional(ReturnCode) return_code);

Result(ProcessID) Scheduler__unpark_by_pid(SchedulerPtr self, ProcessID pid, ParkReason reason, Optional(ReturnCode) return_code);

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu);
Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu);
Optional(ProcessPtr) Scheduler__find_process_by_pid(SchedulerPtr self, ProcessID pid);
ProcessTablePtr Scheduler__all_processes(SchedulerPtr self, CPUID cpu);
ProcessTable Scheduler__all_processes_for_all_cpus(SchedulerPtr self);
ProcessTable Scheduler__process_snapshot(SchedulerPtr self);

uint64_t Scheduler__process_count(SchedulerPtr self);

Optional(ProcessPtr) Scheduler__save_context(SchedulerPtr self, CPUID cpu, ProcessContextPtr context);

Result(CPUID) Scheduler__schedule(SchedulerPtr self, ProcessPtr new_proc);
Result(SchedulerInitStatus) Scheduler__start(SchedulerPtr self, CPUID cpu);
void Scheduler__mark_ready(SchedulerPtr self);
bool Scheduler__ready(SchedulerPtr self);
Result(SchedulerInitStatus) Scheduler__wait_until_ready(SchedulerPtr self);
Result(ServiceCode) Scheduler__subscribe_irq(SchedulerPtr self, ProcessPtr process, IRQNumber irq);
Result(ServiceCode) Scheduler__send_irq_event(SchedulerPtr self, IRQNumber irq);
Result(ServiceCode) Scheduler__complete_pending_irqs_for_process(SchedulerPtr self, ProcessPtr process);

#define NEW_SCHEDULER(broker) \
    (Scheduler) { \
        .message_broker = broker, \
        .schedulers = EmptyArray(CPUSchedulers),\
        .irq_services = EmptyMap(IRQServiceMap),\
        .scheduler_lock = MUTEX_INIT, \
        .ready = false, \
    } \

#endif

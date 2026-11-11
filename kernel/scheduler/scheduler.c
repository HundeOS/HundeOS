#include "scheduler.h"
#include "../arch/process.h"
#include "../arch/page_table.h"
#include "../arch/mmu.h"
#include "../cpu.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "../kalloc.h"
#include "../print.h"
#include "../all_defs.h"
#include "../kernel_core.h"
#include "../panic.h"
#include "../exec/load.h"
#include "../spin.h"
#include "parked.h"
#include "../syscall/deferred.h"
#include "../service/service.h"
#include "idle_debug.h"
#include "shared_wait.h"
#include "preferred.h"

HashtableBodyDefine(CPUSchedulers);
FixedArrayBodyDefine(ManagedSharedPages);

#define CPU_SCHEDULER_INIT(idle, cpu)                                                                                  \
    (CPUScheduler)                                                                                                     \
    {                                                                                                                  \
        .idle_process = idle, .processes = EmptyArray(ProcessTable), .threads = EmptyArray(ThreadTable),               \
        .scheduler_lock = MUTEX_INIT, .last_tick = 0, .cpu = cpu, .registry_slot = 0, .started = false,                \
    }

BString unknown_error = bstr("Unknown error\n");
BString process_not_found = bstr("Process not found\n");
static BString process_permission_denied = bstr("Process permission denied\n");
BString reason_mismatch_format = bstr("Process was parked for reason %i, not %i\n");
BString process_wasnt_parked = bstr("Process wasn't parked\n");
static BString abort_format =
    bstr("process fault: pid=%i name=%s reason=%s exception=%s fsc=%i:%s access=%s pc=0x%llx far=0x%llx "
         "esr=0x%llx ec=0x%llx cpu=%i\n");
static const BString data_abort_reason = bstr("data abort");
static const BString instruction_abort_reason = bstr("instruction abort");
static const BString undefined_instruction_reason = bstr("undefined instruction");
static const BString alignment_fault_reason = bstr("alignment fault");
static const BString bad_syscall_reason = bstr("bad syscall");
static const BString unknown_user_exception_reason = bstr("unknown user exception");
static const BString unknown_fault_reason = bstr("unknown fault");
static BString service_event_queue_empty = bstr("Service event queue is empty");
static BString child_event_queue_empty = bstr("Child event queue is empty");
static BString missing_interrupt_controller = bstr("Missing interrupt controller info");
static BString no_cpus_available = bstr("No CPUs available");

static Result(ServiceCode) Scheduler__service_mark_runnable_locked(SchedulerPtr self, ProcessPtr process);
static Result(ServiceCode)
    Scheduler__child_mark_runnable_locked(SchedulerPtr self, ProcessPtr process, ProcessID child_pid,
                                          ProcessGroupID child_process_group_id, uint64_t event_type);
static Result(ServiceCode)
    Scheduler__service_ready_mark_runnable_locked(SchedulerPtr self, ProcessPtr process, ServiceCode result);
static Result(ServiceCode) Scheduler__notify_parent_child_exit_locked(SchedulerPtr self, ProcessPtr parent,
                                                                      ProcessPtr child, ReturnCode return_code);
static bool Scheduler__is_parked_for(SchedulerPtr self, ProcessPtr proc, ParkReason reason);
static ProcessTimes Scheduler__process_times_at(ProcessPtr process, CPUTime now);
static Result(ServiceCode)
    Scheduler__notify_parent_child_state_locked(SchedulerPtr self, ProcessPtr parent, ProcessPtr child,
                                                uint64_t event_type, ProcessSignal signal);
static Result(ProcessPtr) Scheduler__park_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context, OptionalPtr(Continuation) kernel_continuation,
    ParkReason reason, bool owns_kernel_context, OptionalPtr(OperationKey) operation_key, Anything operation_context);
static Result(ProcessPtr)
    Scheduler__unpark_with_operation_key(SchedulerPtr self, ProcessPtr proc, ParkReason reason,
                                         OptionalPtr(ReturnCode) return_code, OptionalPtr(OperationKey) operation_key);
static Result(ProcessPtr)
    CPUScheduler__unpark_with_operation_key(CPUSchedulerPtr self, ProcessPtr proc, ParkReason reason,
                                            OptionalPtr(ReturnCode) return_code,
                                            OptionalPtr(OperationKey) operation_key);
static Optional(ProcessPtr) Scheduler__current_process_locked(SchedulerPtr self, CPUID cpu);
static ProcessTable Scheduler__all_processes_for_all_cpus_locked(SchedulerPtr self);
static ProcessTable Scheduler__process_snapshot_locked(SchedulerPtr self);
static bool Scheduler__queue_wait_changed(ThreadPtr thread);

#ifdef KERNEL_COMMAND_TRACE
static bool Scheduler__command_flow_process(ProcessPtr process)
{
    return BString__equals(&process->name, &bstr("/usr/local/bin/bash"));
}

static void Scheduler__trace_queue_data_available(ProcessPtr process, ProcessPtr peer, bool reschedule, CPUID cpu,
                                                  QueueConnectionID connection)
{
    if (Scheduler__command_flow_process(process) || Scheduler__command_flow_process(peer))
    {
        __d_printf(COMMAND_FLOW,
                   "t=%i queue sender=%i:%s receiver=%i:%s receiver_state=%s wake=%i cpu=%i connection=%i\n",
                   CPU__time_usec(), process->pid, cstr(process->name), peer->pid, cstr(peer->name),
                   ProcessState__to_string(peer->thread.state), reschedule, cpu, connection);
    }
}

static void Scheduler__trace_child_exit(ProcessPtr parent, ProcessPtr child, ConstProcessChildEventPtr event,
                                        bool retains_child_event)
{
    if (Scheduler__command_flow_process(parent))
    {
        __d_printf(COMMAND_FLOW, "t=%i child-exit child=%i:%s parent=%i:%s parent_state=%s status=%i retained=%i\n",
                   CPU__time_usec(), child->pid, cstr(child->name), parent->pid, cstr(parent->name),
                   ProcessState__to_string(parent->thread.state), event->status, retains_child_event);
    }
}

static void Scheduler__trace_child_wake(ProcessPtr process, ProcessID child_pid, uint64_t event_type, bool wait_ready)
{
    if (Scheduler__command_flow_process(process))
    {
        __d_printf(COMMAND_FLOW, "t=%i child-wake parent=%i:%s child=%i event=%i wait_ready=%i state=%s\n",
                   CPU__time_usec(), process->pid, cstr(process->name), child_pid, event_type, wait_ready,
                   ProcessState__to_string(process->thread.state));
    }
}
#else
static void Scheduler__trace_queue_data_available(ProcessPtr process, ProcessPtr peer, bool reschedule, CPUID cpu,
                                                  QueueConnectionID connection)
{
}

static void Scheduler__trace_child_exit(ProcessPtr parent, ProcessPtr child, ConstProcessChildEventPtr event,
                                        bool retains_child_event)
{
}

static void Scheduler__trace_child_wake(ProcessPtr process, ProcessID child_pid, uint64_t event_type, bool wait_ready)
{
}
#endif

static bool CPUScheduler__owns_process(CPUSchedulerPtr self, ProcessPtr process)
{
    return !is_nothing(ProcessTable__find_item(&self->processes, process));
}

static Optional(CPUSchedulerPtr) Scheduler__scheduler_owning_process_locked(SchedulerPtr self, ProcessPtr process)
{
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu_id,
        cpu,
        {
            if (CPUScheduler__owns_process(cpu, process))
            {
                return Some(CPUSchedulerPtr, cpu);
            }
        }
    );

    return None(CPUSchedulerPtr);
}

static BString shared_memory_error = bstr("Shared page operation failed");

static ManagedSharedPage ManagedSharedPage__new(UserAddress address, ProcessID owner, ProcessID first, ProcessID second)
{
    return (ManagedSharedPage){
        .address = address,
        .owner_pid = owner,
        .first_peer_pid = first,
        .second_peer_pid = second,
    };
}

bool ManagedSharedPage__equals(void * first, void * second)
{
    ManagedSharedPagePtr a = first;
    ManagedSharedPagePtr b = second;
    return a->address.virtual_addr == b->address.virtual_addr;
}

static bool Scheduler__shared_address_available(ProcessPtr process, Address address)
{
    for_each_ll(
        UserAllocations,
        process->address_space.user_allocations,
        allocation,
        {
            Address start = allocation.address.virtual_addr;
            if (address < start + allocation.size && start < address + MEM_SHARED_PAGE_SIZE)
            {
                return false;
            }
        }
    );

    return true;
}

static void Scheduler__unmap_shared_peer_locked(SchedulerPtr self, ProcessID pid, UserAddress address)
{
    if (pid == 0)
    {
        return;
    }
    match_optional(
        Scheduler__find_process_by_pid_locked(self, pid),
        some(process)
        {
            PageTable__unmap(&process->address_space.page_table, process->kernel, address.virtual_addr,
                             MEM_SHARED_PAGE_SIZE);
        },
        none_do_nothing()
    );
}

static Result(MemStatus) Scheduler__release_shared_page_locked(SchedulerPtr self, ManagedSharedPagePtr page)
{
    Scheduler__unmap_shared_peer_locked(self, page->owner_pid, page->address);
    Scheduler__unmap_shared_peer_locked(self, page->first_peer_pid, page->address);
    Scheduler__unmap_shared_peer_locked(self, page->second_peer_pid, page->address);
    // PageTable__unmap completes the broadcast TLB invalidation before backing is freed.
    match_result(
        Kernel__free(page->address.kernel_addr.virtual_addr),
        success(_)
        {
            *page = ManagedSharedPage__new(NULL_USER_ADDRESS, 0, 0, 0);
            return Success(MemStatus, MEM_OK);
        },
        error(code, message)
        {
            return Error(MemStatus, message, MEM_FREE_FAILED);
        }
    );
}

static Result(MemStatus) Scheduler__shared_pages_exit_locked(SchedulerPtr self, ProcessPtr process)
{
    for_each_fa(
        ManagedSharedPages,
        self->shared_pages,
        index,
        value,
        {
            match_optional(
                ManagedSharedPages__ptr_at(&self->shared_pages, index),
                some(page)
                {
                    if (!page->address.valid ||
                        (page->owner_pid != process->pid && page->first_peer_pid != process->pid &&
                         page->second_peer_pid != process->pid))
                    {
                        continue;
                    }
                    PageTable__unmap(&process->address_space.page_table, process->kernel,
                                     page->address.virtual_addr, MEM_SHARED_PAGE_SIZE);
                    if (page->owner_pid == process->pid)
                    {
                        match_result(
                            Scheduler__release_shared_page_locked(self, page),
                            success_do_nothing(),
                            error(code, message)
                            {
                                return Error(MemStatus, message, code);
                            }
                        );
                    }
                    else
                    {
                        if (page->first_peer_pid == process->pid)
                        {
                            page->first_peer_pid = 0;
                        }
                        if (page->second_peer_pid == process->pid)
                        {
                            page->second_peer_pid = 0;
                        }
                    }
                },
                none_do_nothing()
            );
        }
    );
    return Success(MemStatus, MEM_OK);
}

static Result(Address) Scheduler__alloc_shared_page_locked(SchedulerPtr self, ProcessPtr owner,
                                                           OptionalPtr(ProcessPtr) first, ProcessPtr second)
{
    if (ManagedSharedPages__len(&self->shared_pages) < MEM_SHARED_PAGE_COUNT)
    {
        match_result(
            ManagedSharedPages__push(&self->shared_pages, ManagedSharedPage__new(NULL_USER_ADDRESS, 0, 0, 0)),
            success_do_nothing(),
            error(code, message)
            {
                return Error(Address, message, MEM_ALLOCATION_FAILED);
            }
        );
    }
    for_each_fa(
        ManagedSharedPages,
        self->shared_pages,
        index,
        value,
        {
            if (value.address.valid)
            {
                continue;
            }
            Address virtual_address = MEM_SHARED_PAGE_BASE + index * MEM_SHARED_PAGE_SIZE;
            bool first_available = true;
            match_optional(
                *first,
                some(process)
                {
                    first_available = Scheduler__shared_address_available(process, virtual_address);
                },
                none_do_nothing()
            );
            if (!Scheduler__shared_address_available(owner, virtual_address) || !first_available ||
                !Scheduler__shared_address_available(second, virtual_address))
            {
                continue;
            }
            match_optional(
                ManagedSharedPages__ptr_at(&self->shared_pages, index),
                some(page)
                {
                    match_result(
                        Kernel__try_alloc_user(virtual_address, MEM_SHARED_PAGE_SIZE, true, true, false),
                        success(address)
                        {
                            Kernel__memset(address.kernel_addr.virtual_addr, 0, MEM_SHARED_PAGE_SIZE);
                            PageTable__map_shared(&owner->address_space.page_table, owner->kernel, address,
                                                  MEM_SHARED_PAGE_SIZE);
                            ProcessID first_pid = QUEUE_KERNEL_PID;
                            match_optional(
                                *first,
                                some(process)
                                {
                                    first_pid = process->pid;
                                    PageTable__map_shared(&process->address_space.page_table, process->kernel, address,
                                                          MEM_SHARED_PAGE_SIZE);
                                },
                                none_do_nothing()
                            );
                            PageTable__map_shared(&second->address_space.page_table, second->kernel, address,
                                                  MEM_SHARED_PAGE_SIZE);
                            *page = ManagedSharedPage__new(address, owner->pid, first_pid, second->pid);
                            page->generation = ++self->shared_page_generation;
                            return Success(Address, virtual_address);
                        },
                        error(code, message)
                        {
                            return Error(Address, message, MEM_ALLOCATION_FAILED);
                        }
                    );
                },
                none_do_nothing()
            );
        }
    );
    return Error(Address, shared_memory_error, MEM_ALLOCATION_FAILED);
}

Result(Address)
    Scheduler__alloc_shared_page(SchedulerPtr self, ProcessPtr owner, ProcessID first_peer, ProcessID second_peer)
{
    if (!Process__has_capability(owner, PROCESS_CAP_MANAGE_SHARED_PAGES) ||
        !ProcessClass__is_service(owner->scheduling.process_class))
    {
        return Error(Address, shared_memory_error, MEM_PERMISSION_DENIED);
    }
    if (first_peer == 0 || second_peer == 0 || first_peer == second_peer || first_peer == owner->pid ||
        second_peer == owner->pid)
    {
        return Error(Address, shared_memory_error, MEM_PEER_NOT_FOUND);
    }
    critical_section(
        self->scheduler_lock,
        {
            if (self->shared_page_generation == UINT64_MAX)
            {
                cs_return(Error(Address, shared_memory_error, MEM_ALLOCATION_FAILED));
            }
            if (first_peer == QUEUE_KERNEL_PID)
            {
                uint64_t warden =
                    unwrap_optional(ServiceDirectory__find_service_pid(
                                        KernelServiceDirectory__kernel_directory(&owner->kernel->service_directory),
                                        &bstr("hundekanal-warter")),
                                    0);
                if (owner->pid != warden || !Queues__has_kernel_client(&self->queues, owner))
                {
                    cs_return(Error(Address, shared_memory_error, MEM_PERMISSION_DENIED));
                }
                match_optional(
                    Scheduler__find_process_by_pid_locked(self, second_peer),
                    some(second)
                    {
                        cs_return(Scheduler__alloc_shared_page_locked(self, owner, &None(ProcessPtr), second));
                    },
                    none_do_nothing()
                );
                cs_return(Error(Address, shared_memory_error, MEM_PEER_NOT_FOUND));
            }
            match_optional(
                Scheduler__find_process_by_pid_locked(self, first_peer),
                some(first)
                {
                    match_optional(
                        Scheduler__find_process_by_pid_locked(self, second_peer),
                        some(second)
                        {
                            cs_return(
                                Scheduler__alloc_shared_page_locked(self, owner, &Some(ProcessPtr, first), second));
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );
    return Error(Address, shared_memory_error, MEM_PEER_NOT_FOUND);
}

Result(MemStatus) Scheduler__release_shared_page(SchedulerPtr self, ProcessPtr owner, Address address)
{
    if (address < MEM_SHARED_PAGE_BASE ||
        address >= MEM_SHARED_PAGE_BASE + MEM_SHARED_PAGE_COUNT * MEM_SHARED_PAGE_SIZE ||
        address % MEM_SHARED_PAGE_SIZE != 0)
    {
        return Error(MemStatus, shared_memory_error, MEM_INVALID_ADDRESS);
    }
    critical_section(
        self->scheduler_lock,
        {
            uint64_t index = (address - MEM_SHARED_PAGE_BASE) / MEM_SHARED_PAGE_SIZE;
            match_optional(
                ManagedSharedPages__ptr_at(&self->shared_pages, index),
                some(page)
                {
                    if (page->address.valid)
                    {
                        if (page->owner_pid != owner->pid)
                        {
                            cs_return(Error(MemStatus, shared_memory_error, MEM_PERMISSION_DENIED));
                        }
                        cs_return(Scheduler__release_shared_page_locked(self, page));
                    }
                },
                none_do_nothing()
            );
        }
    );
    return Error(MemStatus, shared_memory_error, MEM_INVALID_ADDRESS);
}

static Result(ProcessPtr) Scheduler__detach_exiting_process(SchedulerPtr self, CPUSchedulerPtr cpu, ProcessPtr process,
                                                            ReturnCode return_code);

Result(Address) Scheduler__queue_listen(SchedulerPtr self, ProcessPtr process, Address name)
{
    critical_section(
        self->scheduler_lock,
        {
            BString snapshot = empty_bstr();
            match_result(
                Process__copy_from_user(process, name, (DataPtr)&snapshot, sizeof(snapshot)),
                success_do_nothing(),
                error(code, message)
                {
                    cs_return(Error(Address, bstr("Invalid endpoint name mapping"), QUEUE_INVALID));
                }
            );
            if (!BString__is_valid(&snapshot) || BString__is_empty(&snapshot) ||
                BString__length(&snapshot) >= HUND_SERVICE_NAME_SIZE)
            {
                cs_return(Error(Address, bstr("Invalid endpoint name"), QUEUE_INVALID));
            }
            BString endpoint_name = BString__slice(&snapshot, 0, BString__length(&snapshot));
            cs_return(Queues__listen(&self->queues, process, &endpoint_name));
        }
    );
    return Error(Address, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

Result(Address) Scheduler__queue_operation(SchedulerPtr self, ProcessPtr process, uint64_t operation, uint64_t id)
{
    critical_section(
        self->scheduler_lock,
        {
            if (operation == SERVICE_QUEUE_CONNECT)
            {
                match_result(
                    Queues__connect(&self->queues, process, (QueueEndpointID)id),
                    success(connection)
                    {
                        cs_return(Success(Address, connection));
                    },
                    error(code, message)
                    {
                        cs_return(Error(Address, message, code));
                    }
                );
            }
            if (operation == SERVICE_QUEUE_ACCEPT)
            {
                match_result(
                    Queues__accept(&self->queues, process, (QueueConnectionID)id),
                    success(queue_code)
                    {
                        cs_return(Success(Address, queue_code));
                    },
                    error(code, message)
                    {
                        cs_return(Error(Address, message, code));
                    }
                );
            }
            if (operation == SERVICE_QUEUE_CLOSE)
            {
                match_result(
                    Queues__close(&self->queues, process, (QueueConnectionID)id),
                    success(queue_code)
                    {
                        cs_return(Success(Address, queue_code));
                    },
                    error(code, message)
                    {
                        cs_return(Error(Address, message, code));
                    }
                );
            }
        }
    );
    return Error(Address, bstr("Invalid queue operation"), QUEUE_INVALID);
}

Result(Address) Scheduler__queue_delegate(SchedulerPtr self, ProcessPtr process, QueueConnectionID client_connection,
                                          QueueEndpointID endpoint)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                Queues__delegate(&self->queues, process, client_connection, endpoint),
                success(connection)
                {
                    cs_return(Success(Address, connection));
                },
                error(code, message)
                {
                    cs_return(Error(Address, message, code));
                }
            );
        }
    );
    return Error(Address, bstr("Queue delegation failed"), QUEUE_INVALID);
}

Result(Address) Scheduler__queue_cancel_delegation(SchedulerPtr self, ProcessPtr process,
                                                   QueueConnectionID client_connection, QueueConnectionID delegated)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                Queues__cancel_delegation(&self->queues, process, client_connection, delegated),
                success(code)
                {
                    cs_return(Success(Address, code));
                },
                error(code, message)
                {
                    cs_return(Error(Address, message, code));
                }
            );
        }
    );
    return Error(Address, bstr("Queue delegation cancellation failed"), QUEUE_INVALID);
}

static bool Scheduler__process_has_runnable_thread_locked(SchedulerPtr self, ProcessPtr process)
{
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        scheduler,
        {
            for_each_fa(
                ThreadTable,
                scheduler->threads,
                thread_index,
                thread,
                {
                    if (thread->owner == process && (thread->state == PROC_STATE_READY ||
                                                     thread->state == PROC_STATE_INTERRUPTED))
                    {
                        return true;
                    }
                }
            );
        }
    );
    return false;
}

typedef struct QueueWaitWake
{
    bool had_ready_waiter;
    bool woke_waiter;
} QueueWaitWake;

static QueueWaitWake QueueWaitWake__new(void)
{
    return (QueueWaitWake){.had_ready_waiter = false, .woke_waiter = false};
}

static QueueWaitWake Scheduler__wake_kernel_queue_waiters_locked(SchedulerPtr self, ProcessPtr process)
{
    QueueWaitWake wake = QueueWaitWake__new();
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        scheduler,
        {
            for_each_fa(
                ThreadTable,
                scheduler->threads,
                thread_index,
                thread,
                {
                    if (thread->owner != process)
                    {
                        continue;
                    }
                    if (Thread__has_ready_parked_operation(thread, PARK_WAIT_QUEUE, &None(OperationKey)))
                    {
                        wake.had_ready_waiter = true;
                    }
                    if (thread->state == PROC_STATE_WAITING &&
                        Thread__has_parked_operation(thread, PARK_WAIT_QUEUE, &None(OperationKey)) &&
                        Thread__kernel_queue_wait_ready(thread) &&
                        Thread__mark_parked_operation_ready(thread, PARK_WAIT_QUEUE, &None(OperationKey),
                                                            &Some(ReturnCode, QUEUE_WAIT_CHANGED)))
                    {
                        thread->state = PROC_STATE_INTERRUPTED;
                        QueueWaitUsage__wake(&thread->queue_wait_usage, QUEUE_WAIT_CHANGED);
                        Thread__clear_kernel_queue_wait(thread);
                        wake.woke_waiter = true;
                    }
                }
            );
        }
    );
    return wake;
}

static Result(SchedulerWake) Scheduler__wake_shared_wait_locked(SchedulerPtr self, ProcessPtr process)
{
    bool userspace_waiting = Process__shared_wait_armed(process);
    Process__wake_shared_wait(process);
    QueueWaitWake wake = Scheduler__wake_kernel_queue_waiters_locked(self, process);
    if ((!userspace_waiting && !wake.woke_waiter) || !Scheduler__process_has_runnable_thread_locked(self, process))
    {
        return Success(SchedulerWake, SchedulerWake__new(false, 0));
    }

    match_optional(
        Scheduler__scheduler_owning_process_locked(self, process),
        some(scheduler)
        {
            return Success(SchedulerWake, SchedulerWake__new(true, scheduler->cpu));
        },
        none_do_nothing()
    );
    return Error(SchedulerWake, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(SchedulerWake) Scheduler__wake_queue_wait_locked(SchedulerPtr self, ProcessPtr process)
{
    QueueWaitWake wake = Scheduler__wake_kernel_queue_waiters_locked(self, process);
    if (!Process__has_pending_messages(process) || !Scheduler__process_has_runnable_thread_locked(self, process) ||
        (wake.had_ready_waiter && !wake.woke_waiter && CPU__count() != 1))
    {
        return Success(SchedulerWake, SchedulerWake__new(false, 0));
    }

    match_optional(
        Scheduler__scheduler_owning_process_locked(self, process),
        some(scheduler)
        {
            return Success(SchedulerWake, SchedulerWake__new(true, scheduler->cpu));
        },
        none_do_nothing()
    );
    return Error(SchedulerWake, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(SchedulerWake) Scheduler__wake_shared_wait(SchedulerPtr self, ProcessPtr process)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Scheduler__wake_shared_wait_locked(self, process));
        }
    );
    return Error(SchedulerWake, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(SchedulerWake)
    Scheduler__queue_data_available(SchedulerPtr self, ProcessPtr process, QueueConnectionID connection)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                Queues__peer_process(&self->queues, process, connection),
                success(peer)
                {
                    match_result(
                        Scheduler__wake_queue_wait_locked(self, peer),
                        success(wake)
                        {
                            Scheduler__trace_queue_data_available(process, peer, wake.reschedule, wake.cpu,
                                                                  connection);
                            cs_return(Success(SchedulerWake, wake));
                        },
                        error(code, message)
                        {
                            cs_return(Error(SchedulerWake, message, code));
                        }
                    );
                },
                error(code, message)
                {
                    cs_return(Error(SchedulerWake, message, code));
                }
            );
        }
    );

    return Error(SchedulerWake, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

static Tick CPUScheduler__sleep_ticks_from_usec(uint64_t usec)
{
    Tick ticks = usec / PROCESS_CPU_TICK_USEC + (usec % PROCESS_CPU_TICK_USEC != 0);
    return ticks == 0 ? 1 : ticks;
}

static void CPUScheduler__wake_expired_sleepers(CPUSchedulerPtr self)
{
    uint64_t now_usec = CPU__time_usec();
    for_each_fa(
        ThreadTable,
        self->threads,
        thread_index,
        thread,
        {
            Thread__wake_expired_sleep(thread, self->last_tick);
        }
    );
    for_each_fa(
        ProcessTable,
        self->processes,
        index,
        proc,
        {
            if (Process__take_expired_alarm(proc, now_usec))
            {
                Process__queue_signal(proc, PROCESS_SIGNAL_ALARM);
                Process__wake_shared_wait(proc);
            }
        }
    );
}

Result(CPUSchedulerPtr) CPUScheduler__heap_new(KernelPtr kernel, Elf idle_process, CPUID cpu)
{
    Optional(ProcessPtr) parent = None(ProcessPtr);
    match_result(
        Process__setup(kernel, &idle_process, &parent),
        success(idle)
        {
            CPUSchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(CPUScheduler)), CPUScheduler);
            *sched = CPU_SCHEDULER_INIT(idle, cpu);
            match_result(
                Process__map_idle_debug_page(idle),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(CPUSchedulerPtr, message, code);
                }
            );
            return Success(CPUSchedulerPtr, sched);
        },
        error(code, message)
        {
            return Error(CPUSchedulerPtr, message, code);
        }
    );
}

static bool thread_in_state(ThreadPtr * thread, void * compare_state)
{
    ProcessState state = (*thread)->state;
    ProcessState compare = *(ProcessStatePtr)compare_state;
    __d_printf(SCHEDULER, "thread_in_state %s %s\n", cstr((*thread)->owner->name), ProcessState__to_string(state));

    return state & compare;
}

static bool CPUScheduler__can_resume(ThreadPtr * thread, void * context)
{
    ProcessPtr proc = (*thread)->owner;
    if (proc->lifecycle.terminating)
    {
        return false;
    }
    switch ((*thread)->state)
    {
    case PROC_STATE_READY:
    case PROC_STATE_INTERRUPTED:
        return Thread__shared_wait_ready(*thread);
    case PROC_STATE_WAITING:
    case PROC_STATE_SLEEPING:
        return Thread__can_resume_for_signal(*thread);
    case PROC_STATE_STOPPED:
        return Process__has_pending_resume_signal(proc);
    default:
        return false;
    }
}

static void CPUScheduler__publish_processes(CPUSchedulerPtr self)
{
    KernelProcessRegistryPtr registry = &self->idle_process->kernel->process_registry;
    uint64_t count = 0;
    if (self->started)
    {
        ProcessRecord idle = ProcessRecord__new(self->idle_process, self->cpu);
        KernelProcessRegistry__set(registry, self->registry_slot, count++, &idle);
    }
    for_each_fa(
        ProcessTable,
        self->processes,
        index,
        process,
        {
            ProcessRecord record = ProcessRecord__new(process, self->cpu);
            KernelProcessRegistry__set(registry, self->registry_slot, count++, &record);
        }
    );

    KernelProcessRegistry__set_count(registry, self->registry_slot, count);
    self->last_registry_sample_usec = CPU__time_usec();
}

static Result(CPUID) CPUScheduler__schedule(CPUSchedulerPtr self, ProcessPtr new_proc)
{
    __d_printf(SCHEDULER, "Queuing %s %s\n", cstr(new_proc->name), ProcessState__to_string(new_proc->thread.state));
    match_result(
        ThreadTable__push(&self->threads, &new_proc->thread),
        success(index)
        {
            match_result(
                ProcessTable__push(&self->processes, new_proc),
                success(process_index)
                {
                    new_proc->thread.state = PROC_STATE_READY;
                    __d_printf(SCHEDULER, "%s %s\n", cstr(new_proc->name),
                               ProcessState__to_string(new_proc->thread.state));
                    CPUScheduler__publish_processes(self);
                    return Success(CPUID, self->cpu);
                },
                error(code, message)
                {
                    ThreadTable__remove(&self->threads, &new_proc->thread);
                    return Error(CPUID, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(CPUID, message, code);
        }
    );
}

static Optional(ProcessPtr) Scheduler__take_admitted(SchedulerPtr self, CPUSchedulerPtr cpu)
{
    match_optional(
        ProcessTable__at(&self->admission, 0),
        some(process)
        {
            match_result(
                CPUScheduler__schedule(cpu, process),
                success(scheduled_cpu)
                {
                    ProcessTable__pop_front(&self->admission);
                    return Some(ProcessPtr, process);
                },
                error(code, message)
                {
                    __d_printf(SCHEDULER, "Unable to admit process pid=%i on cpu=%i: %s\n", process->pid, cpu->cpu,
                               cstr(message));
                }
            );
        },
        none_do_nothing()
    );

    return None(ProcessPtr);
}

static int longest_waiting(ThreadPtr * first, ThreadPtr * second)
{
    if ((*second)->last_tick > (*first)->last_tick)
    {
        return 1;
    }

    if ((*second)->last_tick < (*first)->last_tick)
    {
        return -1;
    }

    return 0;
}

static int service_priority_then_longest_waiting(ThreadPtr * first, ThreadPtr * second)
{
    ProcessPriority proc1_priority = ProcessClass__priority((*first)->owner->scheduling.process_class);
    ProcessPriority proc2_priority = ProcessClass__priority((*second)->owner->scheduling.process_class);

    if (proc1_priority > proc2_priority)
    {
        return 1;
    }

    if (proc1_priority < proc2_priority)
    {
        return -1;
    }

    return longest_waiting(first, second);
}

static bool thread_has_pending_messages(ThreadPtr * thread, void * context)
{
    return Process__has_pending_messages((*thread)->owner);
}

static Optional(ThreadPtr) CPUScheduler__select_ready(CPUSchedulerPtr self)
{
    SchedulerPtr scheduler = self->idle_process->kernel->scheduler;
    match_optional(
        Scheduler__take_preferred(scheduler, self),
        some(process)
        {
            return Some(ThreadPtr, &process->thread);
        },
        none_do_nothing()
    );
    match_optional(
        Scheduler__take_admitted(scheduler, self),
        some(process)
        {
            return Some(ThreadPtr, &process->thread);
        },
        none_do_nothing()
    );
    ThreadTable ready = ThreadTable__filter(&self->threads, CPUScheduler__can_resume, self);
    ThreadTable receivers = ThreadTable__filter(&ready, thread_has_pending_messages, self);
    ThreadTable sorted = ThreadTable__sort_desc(ThreadTable__len(&receivers) != 0 ? &receivers : &ready,
                                                service_priority_then_longest_waiting);
    return ThreadTable__at(&sorted, 0);
}

typedef struct SpeculativeWaitCandidate
{
    ProcessPtr process;
    uint64_t dependency_weight;
    uint64_t recent_active_basis_points;
} SpeculativeWaitCandidate;

typedef SpeculativeWaitCandidate * SpeculativeWaitCandidatePtr;
typedef const SpeculativeWaitCandidate * ConstSpeculativeWaitCandidatePtr;
OptionalDef(SpeculativeWaitCandidate);

static bool Process__speculative_wait_eligible(ProcessPtr self, const OptionalPtr(ProcessPtr) current)
{
    if (self->lifecycle.terminating)
    {
        return false;
    }
    bool schedulable = self->thread.state == PROC_STATE_READY || self->thread.state == PROC_STATE_INTERRUPTED;
    match_optional(*current,
        some(process)
        {
            schedulable = schedulable || process == self;
        },
        none_do_nothing()
    );
    return schedulable && !Process__shared_wait_ready(self);
}

static SpeculativeWaitCandidate SpeculativeWaitCandidate__new(SchedulerPtr scheduler, ProcessPtr process)
{
    uint64_t now = CPU__time_usec();
    ProcessCPUUsage usage =
        CPUUsage__snapshot(&process->accounting.cpu_usage, now, Process__user_wait_usec(process, now));
    return (SpeculativeWaitCandidate){
        .process = process,
        .dependency_weight = RequestGraph__dependency_weight(&scheduler->request_graph, process),
        .recent_active_basis_points = usage.short_basis_points,
    };
}

static bool SpeculativeWaitCandidate__precedes(ConstSpeculativeWaitCandidatePtr self,
                                               ConstSpeculativeWaitCandidatePtr other)
{
    if (self->dependency_weight != other->dependency_weight)
    {
        return self->dependency_weight > other->dependency_weight;
    }
    if (self->recent_active_basis_points != other->recent_active_basis_points)
    {
        return self->recent_active_basis_points > other->recent_active_basis_points;
    }

    return self->process->thread.last_tick < other->process->thread.last_tick;
}

static Optional(ProcessPtr)
    CPUScheduler__select_speculative_waiter(CPUSchedulerPtr self, const OptionalPtr(ProcessPtr) current)
{
    SchedulerPtr scheduler = self->idle_process->kernel->scheduler;
    Optional(SpeculativeWaitCandidate) selected = None(SpeculativeWaitCandidate);
    for_each_fa(
        ProcessTable,
        self->processes,
        index,
        process,
        {
            if (!Process__speculative_wait_eligible(process, current))
            {
                continue;
            }
            SpeculativeWaitCandidate candidate = SpeculativeWaitCandidate__new(scheduler, process);
            match_optional(
                selected,
                some(existing)
                {
                    if (SpeculativeWaitCandidate__precedes(&candidate, &existing))
                    {
                        selected = Some(SpeculativeWaitCandidate, candidate);
                    }
                },
                none()
                {
                    selected = Some(SpeculativeWaitCandidate, candidate);
                }
            );
        }
    );

    match_optional(
        selected,
        some(candidate)
        {
            QueueWaitUsage__select_speculative(&candidate.process->thread.queue_wait_usage);
            return Some(ProcessPtr, candidate.process);
        },
        none_do_nothing()
    );
    return None(ProcessPtr);
}

static Optional(ThreadPtr) CPUScheduler__current_thread(CPUSchedulerPtr self)
{
    ProcessState running_state = PROC_STATE_RUNNING;
    FindResultOptional(ThreadTable) current_match = ThreadTable__find(&self->threads, thread_in_state, &running_state);

    match_find_result(
        current_match,
        some(thread)
        {
            return Some(ThreadPtr, thread);
        },
        none()
        {
            if (self->idle_process->thread.state == PROC_STATE_RUNNING)
            {
                return Some(ThreadPtr, &self->idle_process->thread);
            }
            return None(ThreadPtr);
        }
    );

    return None(ThreadPtr);
}

Optional(ProcessPtr) CPUScheduler__current_process(CPUSchedulerPtr self)
{
    match_optional(
        CPUScheduler__current_thread(self),
        some(thread)
        {
            return Some(ProcessPtr, thread->owner);
        },
        none()
        {
            return None(ProcessPtr);
        }
    );
}

bool CPUScheduler__is_idle(CPUSchedulerPtr self)
{
    ProcessState running_state = PROC_STATE_RUNNING;
    FindResultOptional(ThreadTable) current_match = ThreadTable__find(&self->threads, thread_in_state, &running_state);

    match_find_result(current_match,
        some(process)
        {
            return false;
        },
        none()
        {
            return true;
        }
    );

    return true;
}

uint64_t Scheduler__process_count(SchedulerPtr self)
{
    uint64_t count = 0;

    critical_section(
        self->scheduler_lock,
        {
            count = ProcessTable__len(&self->admission);
            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                sched,
                {
                    count += ProcessTable__len(&sched->processes);
                }
            );
        }
    );

    return count;
}

Result(ProcessPtr) CPUScheduler__exit(CPUSchedulerPtr self, ProcessPtr proc)
{
    __d_printf(SCHEDULER, "CPUScheduler__exit %s\n", cstr(proc->name));

    Result(CollectionCode) remove_res;

    critical_section(
        self->scheduler_lock,
        {
            remove_res = ProcessTable__remove(&self->processes, proc);
            match_result(
                remove_res,
                success(index)
                {
                    if (is_something(ThreadTable__find_item(&self->threads, &proc->thread)))
                    {
                        remove_res = ThreadTable__remove(&self->threads, &proc->thread);
                    }
                },
                error(code, message)
                {
                    __d_printf(SCHEDULER, "Missing scheduled process pid=%i: %s\n", proc->pid,
                               cstr(message));
                }
            );
        }
    );

    match_result(
        remove_res,
        success(process_index)
        {
            CPUTime now = CPU__time_usec();
            CPUUsage__stop(&proc->accounting.cpu_usage, now, Process__user_wait_usec(proc, now));
            proc->thread.state = PROC_STATE_DEAD;
            CPUScheduler__publish_processes(self);
            return Success(ProcessPtr, proc);
        },
        error(code, message)
        {
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        }
    );
}

static Result(CollectionCode) Scheduler__register_live_child_locked(SchedulerPtr self, ProcessPtr child)
{
    match_optional(
        child->parent,
        some(parent)
        {
            match_optional(
                child->parent_pid,
                some(parent_pid)
                {
                    match_optional(
                        Scheduler__find_process_by_pid_locked(self, parent_pid),
                        some(live_parent)
                        {
                            if (live_parent == parent)
                            {
                                return LiveChildren__push(&parent->live_children, child);
                            }
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
            child->parent = None(ProcessPtr);
            child->parent_pid = None(ProcessID);
        },
        none_do_nothing()
    );
    return Success(CollectionCode, 0);
}

static void Scheduler__unlink_live_child_locked(ProcessPtr child)
{
    match_optional(
        child->parent,
        some(parent)
        {
            match_result(
                LiveChildren__remove(&parent->live_children, child),
                success_do_nothing(),
                error(code, message)
                {
                    __d_printf(SCHEDULER, "Missing live child pid=%i parent=%i\n", child->pid, parent->pid);
                }
            );
        },
        none_do_nothing()
    );
}

static void Scheduler__orphan_children_locked(ProcessPtr parent)
{
    while (true)
    {
        match_optional(
            LiveChildren__remove_at(&parent->live_children, 0),
            some(child)
            {
                child->parent = None(ProcessPtr);
                child->parent_pid = None(ProcessID);
            },
            none()
            {
                return;
            }
        );
    }
}

static Result(ServiceCode)
    Scheduler__notify_exiting_child_locked(SchedulerPtr self, ProcessPtr child, ReturnCode return_code)
{
    match_optional(
        child->parent,
        some(parent)
        {
            if (!Scheduler__is_parked_for(self, parent, PARK_WAIT_CHILD))
            {
                return Scheduler__notify_parent_child_exit_locked(self, parent, child, return_code);
            }
            match_optional(
                child->lifecycle.abort,
                some(abort)
                {
                    SpawnAbortReport report =
                        SpawnAbortReport__new(abort.reason, abort.exception_code, abort.pc, abort.fault_address);
                    bool written = false;
                    for_each_hashtable(
                        CPUSchedulers,
                        self->schedulers,
                        bucket_index,
                        entry_index,
                        cpu,
                        scheduler,
                        {
                            for_each_fa(
                                ThreadTable,
                                scheduler->threads,
                                thread_index,
                                thread,
                                {
                                    if (!written && thread->owner == parent &&
                                        Thread__write_child_abort_report(thread, report))
                                    {
                                        written = true;
                                    }
                                }
                            );
                        }
                    );
                },
                none_do_nothing()
            );
            ProcessTimes child_times = Scheduler__process_times_at(child, CPU__time_usec());
            parent->accounting.children_user_usec += child_times.user_usec + child_times.children_user_usec;
            parent->accounting.children_system_usec += child_times.system_usec + child_times.children_system_usec;
            __d_printf(
                PROGRESS,
                "scheduler child complete child_pid=%i child_name=%s parent_pid=%i parent_name=%s return_code=%i\n",
                child->pid, cstr(child->name), parent->pid, cstr(parent->name), return_code);
            match_result(
                Scheduler__unpark(self, parent, PARK_WAIT_CHILD, &Some(ReturnCode, return_code)),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ServiceCode, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Success(ServiceCode, SERVICE_OK);
}

static Result(ProcessPtr) Scheduler__detach_exiting_process(SchedulerPtr self, CPUSchedulerPtr cpu, ProcessPtr process,
                                                            ReturnCode return_code)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                CPUScheduler__exit(cpu, process),
                success(exited)
                {
                    for_each_ll(
                        ProcessThreads,
                        exited->threads,
                        thread,
                        {
                            if (thread != &exited->thread)
                            {
                                for_each_hashtable(
                                    CPUSchedulers,
                                    self->schedulers,
                                    bucket_index,
                                    entry_index,
                                    sibling_cpu,
                                    scheduler,
                                    {
                                        if (is_something(ThreadTable__find_item(&scheduler->threads, thread)))
                                        {
                                            ThreadTable__remove(&scheduler->threads, thread);
                                        }
                                        match_optional(
                                            scheduler->switching_out,
                                            some(outgoing)
                                            {
                                                if (outgoing == thread)
                                                {
                                                    scheduler->switching_out = None(ThreadPtr);
                                                }
                                            },
                                            none_do_nothing()
                                        );
                                    }
                                );
                                thread->state = PROC_STATE_DEAD;
                            }
                        }
                    );
                    match_optional(
                        cpu->switching_out,
                        some(outgoing)
                        {
                            if (outgoing == &exited->thread)
                            {
                                cpu->switching_out = None(ThreadPtr);
                            }
                        },
                        none_do_nothing()
                    );
                    Scheduler__forget_preferred(self, exited);
                    Queues__exit(&self->queues, exited);
                    Buffers__exit(&self->buffers, self, exited);
                    match_result(
                        Scheduler__shared_pages_exit_locked(self, exited),
                        success(_)
                        {
                            Scheduler__unlink_live_child_locked(exited);
                            Scheduler__orphan_children_locked(exited);
                            match_result(
                                Scheduler__notify_exiting_child_locked(self, exited, return_code),
                                success_do_nothing(),
                                error(code, message)
                                {
                                    cs_return(Error(ProcessPtr, message, SCHEDULER_GENERAL_ERROR));
                                }
                            );
                            cs_return(Success(ProcessPtr, exited));
                        },
                        error(code, message)
                        {
                            cs_return(Error(ProcessPtr, message, SCHEDULER_GENERAL_ERROR));
                        }
                    );
                },
                error(code, message)
                {
                    cs_return(Error(ProcessPtr, message, code));
                }
            );
        }
    );

    return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(ProcessPtr)
    CPUScheduler__park(CPUSchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                       OptionalPtr(Continuation) kernel_continuation, ParkReason reason, bool owns_kernel_context,
                       OptionalPtr(OperationKey) operation_key, Anything operation_context)
{
    __d_printf(SCHEDULER, "CPUScheduler__park %s\n", cstr(proc->name));
    ThreadPtr thread = &proc->thread;
    bool requested_mismatch = false;
    bool current_matches = false;
    match_optional(
        kernel_context->thread,
        some(requested)
        {
            if (requested->owner == proc)
            {
                thread = requested;
            }
            else
            {
                requested_mismatch = true;
            }
        },
        none_do_nothing()
    );
    match_optional(
        CPUScheduler__current_thread(self),
        some(current)
        {
            if (current->owner == proc)
            {
                thread = current;
                current_matches = true;
            }
        },
        none_do_nothing()
    );
    if (requested_mismatch && !current_matches)
    {
        if (is_nothing(ProcessTable__find_item(&self->processes, proc)))
        {
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        }
        return Error(ProcessPtr, bstr("Parked thread belongs to another process"), SCHEDULER_GENERAL_ERROR);
    }
    match_optional(
        ThreadTable__find_item(&self->threads, thread),
        some(index)
        {
            kernel_context->thread = Some(ThreadPtr, thread);
            ParkedOperationPtr parked = ParkedOperation__heap_new_with_context_ownership(
                kernel_context, kernel_continuation, reason, owns_kernel_context);
            parked->operation_key = *operation_key;
            parked->operation_context = operation_context;
            CPUTime now = CPU__time_usec();
            CPUUsage__stop(&proc->accounting.cpu_usage, now, Process__user_wait_usec(proc, now));
            thread->state = reason == PARK_SLEEP ? PROC_STATE_SLEEPING : PROC_STATE_WAITING;
            __d_printf(SCHEDULER, "CPUScheduler__park - context %i\n", parked);
            match_result(
                Thread__push_parked_operation(thread, parked),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ProcessPtr, message, code);
                }
            );
            return Success(ProcessPtr, proc);
        },
        none()
        {
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        }
    );
}

static bool Scheduler__queue_wait_changed(ThreadPtr thread)
{
    return Thread__kernel_queue_wait_ready(thread);
}

Result(QueueWaitCode)
    Scheduler__queue_wait(SchedulerPtr self, ThreadPtr thread, IpcEventGeneration observed, Tick ticks)
{
    ProcessPtr process = thread->owner;
    if (ticks > QUEUE_WAIT_MAX_TICKS)
    {
        return Error(QueueWaitCode, bstr("Invalid queue wait bounds"), QUEUE_INVALID);
    }
    critical_section(
        self->scheduler_lock,
        {
            if (Process__doorbell_changed(process, observed) || Process__has_pending_signal(process) ||
                Process__has_pending_queue_wait(process))
            {
                cs_return(Success(QueueWaitCode, QUEUE_WAIT_CHANGED));
            }
            Thread__arm_kernel_queue_wait(thread, observed);
            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                sched,
                {
                    match_optional(
                        ThreadTable__find_item(&sched->threads, thread),
                        some(index)
                        {
                            KernelContextPtr context = KernelContext__heap_new(&Some(ThreadPtr, thread),
                                                                               &Some(SchedulerPtr, self));
                            Optional(Continuation) continuation = Some(Continuation, SystemCall__Complete);
                            Optional(OperationKey) deadline = None(OperationKey);
                            if (ticks != QUEUE_WAIT_INDEFINITE_TICKS)
                            {
                                deadline = Some(OperationKey, sched->last_tick + ticks);
                            }
                                         // Mark parked only; the syscall dispatcher switches after lock release.
                            match_result(
                                CPUScheduler__park(sched, process, context, &continuation, PARK_WAIT_QUEUE,
                                                   true, &deadline, observed),
                                success(value)
                                {
                                    QueueWaitUsage__park(&thread->queue_wait_usage);
                                    cs_return(Success(QueueWaitCode, QUEUE_WAIT_PARKED));
                                },
                                error(code, message)
                                {
                                    Thread__clear_kernel_queue_wait(thread);
                                    KernelContext__dispose_heap(context);
                                    cs_return(Error(QueueWaitCode, message, code));
                                }
                            );
                        },
                        none_do_nothing()
                    );
                }
            );
        }
    );

    Thread__clear_kernel_queue_wait(thread);
    return Error(QueueWaitCode, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

static void CPUScheduler__poll_queue_waiters(SchedulerPtr self, CPUSchedulerPtr cpu)
{
    for_each_fa(
        ThreadTable,
        cpu->threads,
        index,
        thread,
        {
            if (thread->state != PROC_STATE_WAITING ||
                !Thread__has_parked_operation(thread, PARK_WAIT_QUEUE, &None(OperationKey)))
            {
                continue;
            }
            if (Scheduler__queue_wait_changed(thread))
            {
                Thread__mark_parked_operation_ready(thread, PARK_WAIT_QUEUE, &None(OperationKey),
                                                    &Some(ReturnCode, QUEUE_WAIT_CHANGED));
                thread->state = PROC_STATE_INTERRUPTED;
                QueueWaitUsage__wake(&thread->queue_wait_usage, QUEUE_WAIT_CHANGED);
                Thread__clear_kernel_queue_wait(thread);
            }
            else
            {
                if (Thread__wake_expired_sleep(thread, cpu->last_tick + 1))
                {
                    Thread__clear_kernel_queue_wait(thread);
                }
            }
        }
    );
}

static void Scheduler__poll_queue_waiters(SchedulerPtr self)
{
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        scheduler,
        {
            CPUScheduler__poll_queue_waiters(self, scheduler);
        }
    );
}

static void Scheduler__poll_exec_load_waiters(CPUSchedulerPtr cpu)
{
    for_each_fa(
        ThreadTable,
        cpu->threads,
        index,
        thread,
        {
            if (thread->state == PROC_STATE_WAITING &&
                Thread__has_parked_operation(thread, PARK_WAIT_EXEC_LOAD, &None(OperationKey)) &&
                ExecLoad__ready(thread->owner))
            {
                Thread__mark_parked_operation_ready(thread, PARK_WAIT_EXEC_LOAD, &None(OperationKey),
                                                    &None(ReturnCode));
                thread->state = PROC_STATE_INTERRUPTED;
            }
        }
    );
}

static Result(ProcessPtr)
    CPUScheduler__unpark(CPUSchedulerPtr self, ProcessPtr proc, ParkReason reason, OptionalPtr(ReturnCode) return_code)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return CPUScheduler__unpark_with_operation_key(self, proc, reason, return_code, &None(OperationKey));
}

static Result(ProcessPtr)
    CPUScheduler__unpark_with_operation_key(CPUSchedulerPtr self, ProcessPtr proc, ParkReason reason,
                                            OptionalPtr(ReturnCode) return_code,
                                            OptionalPtr(OperationKey) operation_key)
{
    __d_printf(SCHEDULER, "CPUScheduler__unpark %s\n", cstr(proc->name));
    match_optional(
        ProcessTable__find_item(&self->processes, proc),
        some(index)
        {
            for_each_fa(
                ThreadTable,
                self->threads,
                thread_index,
                thread,
                {
                    if (thread->owner == proc &&
                        Thread__mark_parked_operation_ready(thread, reason, operation_key, return_code))
                    {
                        thread->state = PROC_STATE_INTERRUPTED;
                        return Success(ProcessPtr, proc);
                    }
                }
            );
            return Error(ProcessPtr, process_wasnt_parked, SCHEDULER_CANT_UNPARK);
        },
        none()
        {
            __d_printf(SCHEDULER, "CPUScheduler__unpark cpu=%i target pid=%i not found\n", self->cpu, proc->pid);
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        }
    );
}

static void CPUScheduler__continue_current(CPUSchedulerPtr self, ThreadPtr thread)
{
    self->switching_out = None(ThreadPtr);
    thread->migration_safe = false;
    thread->state = PROC_STATE_RUNNING;
}

Optional(ContextSwitch) CPUScheduler__interrupt(CPUSchedulerPtr self, const OptionalPtr(ThreadContextPtr) context)
{
    Optional(ThreadPtr) current_thread = CPUScheduler__current_thread(self);
    Optional(ProcessPtr) current = CPUScheduler__current_process(self);
    critical_section(
        self->scheduler_lock,
        {
            self->last_tick++;
            CPUScheduler__wake_expired_sleepers(self);
        }
    );

    match_optional(
        current_thread,
        some(thread)
        {
            ProcessPtr proc = thread->owner;
            __d_printf(SCHEDULER, "CPUScheduler__interrupt %s\n", cstr(proc->name));
            match_result(
                Thread__save_context(thread, context),
                success_do_nothing(),
                error(code, message)
                {
                    __d_printf(SCHEDULER, "Context save failed pid=%i name=%s code=%i message=%s\n", proc->pid,
                               cstr(proc->name), code, cstr(message));
                    return None(ContextSwitch);
                }
            );
            thread->last_tick = self->last_tick;
            if (proc->lifecycle.terminating)
            {
                self->switching_out = Some(ThreadPtr, thread);
                thread->state = PROC_STATE_SWITCHING_CONTEXT;
                self->idle_process->thread.state = PROC_STATE_SWITCHING_CONTEXT;
                return Some(ContextSwitch, ContextSwitch__new(&self->idle_process->thread, &current_thread));
            }
            if (proc != self->idle_process && Process__retain_preferred(proc))
            {
                SchedulerPtr scheduler = proc->kernel->scheduler;
                if (!Scheduler__ready_exec_continuation_waiting(scheduler, proc))
                {
                    CPUScheduler__continue_current(self, thread);
                    return None(ContextSwitch);
                }
                Scheduler__forget_preferred(scheduler, proc);
            }
            self->switching_out = Some(ThreadPtr, thread);
            thread->state = PROC_STATE_SWITCHING_CONTEXT;
        },
        none_do_nothing()
    );

    __d_println(SCHEDULER, "CPUScheduler__interrupt\n");

    Optional(ThreadPtr) selected = CPUScheduler__select_ready(self);
    match_optional(
        selected,
        some(new_thread)
        {
            ProcessPtr new_proc = new_thread->owner;
            match_optional(
                current_thread,
                some(thread)
                {
                    ProcessPtr proc = thread->owner;
                    if (!new_thread->preferred_active && Thread__shared_wait_ready(thread) &&
                        Process__has_pending_messages(proc) && !Process__has_pending_messages(new_proc))
                    {
                        if (Process__has_pending_signal(proc))
                        {
                            return Some(ContextSwitch, ContextSwitch__new(thread, &current_thread));
                        }
                        CPUScheduler__continue_current(self, thread);
                        return None(ContextSwitch);
                    }
                },
                none_do_nothing()
            );
            __d_printf(SCHEDULER, "selected - %p %s\n", new_proc, cstr(new_proc->name));
            new_thread->state = PROC_STATE_SWITCHING_CONTEXT;
            ContextSwitch context_switch = ContextSwitch__new(new_thread, &current_thread);

            return Some(ContextSwitch, context_switch);
        },
        none()
        {
            match_optional(
                CPUScheduler__select_speculative_waiter(self, &current),
                some(waiter)
                {
                    match_optional(
                        current_thread,
                        some(thread)
                        {
                            if (thread == &waiter->thread)
                            {
                                CPUScheduler__continue_current(self, thread);
                                return None(ContextSwitch);
                            }
                        },
                        none_do_nothing()
                    );
                    waiter->thread.state = PROC_STATE_SWITCHING_CONTEXT;
                    return Some(ContextSwitch, ContextSwitch__new(&waiter->thread, &current_thread));
                },
                none_do_nothing()
            );
            match_optional(
                current_thread,
                some(thread)
                {
                    ProcessPtr proc = thread->owner;
                    if (Process__has_pending_signal(proc))
                    {
                        return Some(ContextSwitch, ContextSwitch__new(thread, &current_thread));
                    }
                    if (!Thread__shared_wait_ready(thread))
                    {
                        ProcessPtr idle = self->idle_process;
                        idle->thread.state = PROC_STATE_SWITCHING_CONTEXT;
                        return Some(ContextSwitch, ContextSwitch__new(&idle->thread, &current_thread));
                    }
                    CPUScheduler__continue_current(self, thread);
                    __d_printf(SCHEDULER, "no new proc remain with current - %p %s\n", proc, cstr(proc->name));
                    return None(ContextSwitch);
                },
                none()
                {
                    // no current and nothing ready, go idle
                    ProcessPtr idle = self->idle_process;
                    idle->thread.state = PROC_STATE_SWITCHING_CONTEXT;
                    __d_printf(SCHEDULER, "no new proc or current going idle - %p %s\n", idle, cstr(idle->name));
                    ContextSwitch context_switch = ContextSwitch__new(&idle->thread, &current_thread);
                    return Some(ContextSwitch, context_switch);
                }
            );
        }
    );
    return None(ContextSwitch);
}

static Optional(ContextSwitch) CPUScheduler__yield_waiting(CPUSchedulerPtr self, ThreadPtr waiting_thread)
{
    self->switching_out = Some(ThreadPtr, waiting_thread);
    Optional(ThreadPtr) selected = CPUScheduler__select_ready(self);
    match_optional(
        selected,
        some(new_thread)
        {
            new_thread->state = PROC_STATE_SWITCHING_CONTEXT;
            return Some(ContextSwitch, ContextSwitch__new(new_thread, &Some(ThreadPtr, waiting_thread)));
        },
        none()
        {
            match_optional(
                CPUScheduler__select_speculative_waiter(self, &None(ProcessPtr)),
                some(waiter)
                {
                    waiter->thread.state = PROC_STATE_SWITCHING_CONTEXT;
                    return Some(ContextSwitch, ContextSwitch__new(&waiter->thread, &Some(ThreadPtr, waiting_thread)));
                },
                none_do_nothing()
            );
            ProcessPtr idle = self->idle_process;
            idle->thread.state = PROC_STATE_SWITCHING_CONTEXT;
            return Some(ContextSwitch, ContextSwitch__new(&idle->thread, &Some(ThreadPtr, waiting_thread)));
        }
    );

    return None(ContextSwitch);
}

static Optional(ContextSwitch) CPUScheduler__yield_runnable(CPUSchedulerPtr self, ThreadPtr yielding_thread)
{
    if (yielding_thread->state != PROC_STATE_RUNNING)
    {
        return None(ContextSwitch);
    }

    yielding_thread->state = PROC_STATE_INTERRUPTED;
    self->switching_out = Some(ThreadPtr, yielding_thread);
    match_optional(
        CPUScheduler__select_ready(self),
        some(new_thread)
        {
            if (new_thread == yielding_thread)
            {
                CPUScheduler__continue_current(self, yielding_thread);
                return None(ContextSwitch);
            }
            new_thread->state = PROC_STATE_SWITCHING_CONTEXT;
            return Some(ContextSwitch, ContextSwitch__new(new_thread, &Some(ThreadPtr, yielding_thread)));
        },
        none()
        {
            CPUScheduler__continue_current(self, yielding_thread);
            return None(ContextSwitch);
        }
    );

    return None(ContextSwitch);
}

// __attribute__((noreturn))
void CPUScheduler__start(CPUSchedulerPtr self)
{
    __d_printf(SCHEDULER, "CPUScheduler__start cpu=%i idle pid=%i name=%s state=%s context=%p page_table=%p\n",
               self->cpu, self->idle_process->pid, cstr(self->idle_process->name),
               ProcessState__to_string(self->idle_process->thread.state), Process__required_context(self->idle_process),
               paddr(ProcessPageTable__root(&self->idle_process->address_space.page_table)));
    ContextSwitch context_switch = ContextSwitch__new(&self->idle_process->thread, &None(ThreadPtr));
    ContextSwitch__switch(&context_switch);
}

ProcessTablePtr Scheduler__all_processes(SchedulerPtr self, CPUID cpu)
{
    match_optional(
        CPUSchedulers__get(&self->schedulers, cpu),
        some(scheduler)
        {
            return &scheduler->processes;
        },
        none()
        {
            return NULL; // hundelint: ignore-no-null
        }
    );
}

static ProcessTable Scheduler__all_processes_for_all_cpus_locked(SchedulerPtr self)
{
    ProcessTable processes = ProcessTable__new();
    ProcessTable__push_all(&processes, &self->admission);

    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        sched,
        {
            ProcessTable__push_all(&processes, &sched->processes);
        }
    );

    return processes;
}

ProcessTable Scheduler__all_processes_for_all_cpus(SchedulerPtr self)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Scheduler__all_processes_for_all_cpus_locked(self));
        }
    );

    return ProcessTable__new();
}

static ProcessTable Scheduler__process_snapshot_locked(SchedulerPtr self)
{
    ProcessTable processes = ProcessTable__new();
    ProcessTable__push_all(&processes, &self->admission);

    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        sched,
        {
            if (sched->started)
            {
                ProcessTable__push(&processes, sched->idle_process);
            }
            ProcessTable__push_all(&processes, &sched->processes);
        }
    );

    return processes;
}

ProcessTable Scheduler__process_snapshot(SchedulerPtr self)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Scheduler__process_snapshot_locked(self));
        }
    );

    return ProcessTable__new();
}

static ProcessInfo ProcessInfo__from_process_locked(ProcessPtr process)
{
    ProcessRecord record = ProcessRecord__new(process, 0);
    return ProcessRecord__info(&record);
}

ProcessListResult Scheduler__process_info_snapshot(SchedulerPtr self, ProcessInfoPtr entries, uint64_t max_entries)
{
    uint64_t written = 0;

    critical_section(
        self->scheduler_lock,
        {
            for_each_fa(
                ProcessTable,
                self->admission,
                process_index,
                process,
                {
                    if (written >= max_entries)
                    {
                        cs_return((ProcessListResult)written);
                    }
                    entries[written] = ProcessInfo__from_process_locked(process);
                    entries[written].cpu = (CPUID)SCHEDULER_INVALID_CPU;
                    written++;
                }
            );
            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                sched,
                {
                    if (written >= max_entries)
                    {
                        cs_return((ProcessListResult)written);
                    }

                    if (sched->started)
                    {
                        entries[written] =
                            ProcessInfo__from_process_locked(sched->idle_process);
                        entries[written].cpu = sched->cpu;
                        written++;
                        if (written >= max_entries)
                        {
                            cs_return((ProcessListResult)written);
                        }
                    }

                    for_each_fa(
                        ProcessTable,
                        sched->processes,
                        process_index,
                        process,
                        {
                            if (written >= max_entries)
                            {
                                cs_return((ProcessListResult)written);
                            }
                            entries[written] =
                                ProcessInfo__from_process_locked(process);
                            entries[written].cpu = sched->cpu;
                            written++;
                        }
                    );
                }
            );
        }
    );

    return (ProcessListResult)written;
}

static ConstBStringPtr ProcessAbortReason__to_string(ProcessAbortReason reason)
{
    switch (reason)
    {
    case PROCESS_ABORT_DATA_ABORT:
        return &data_abort_reason;
    case PROCESS_ABORT_INSTRUCTION_ABORT:
        return &instruction_abort_reason;
    case PROCESS_ABORT_UNDEFINED_INSTRUCTION:
        return &undefined_instruction_reason;
    case PROCESS_ABORT_ALIGNMENT:
        return &alignment_fault_reason;
    case PROCESS_ABORT_BAD_SYSCALL:
        return &bad_syscall_reason;
    case PROCESS_ABORT_UNKNOWN_USER_EXCEPTION:
        return &unknown_user_exception_reason;
    default:
        return &unknown_fault_reason;
    }
}

static bool Scheduler__is_parked_for(SchedulerPtr self, ProcessPtr proc, ParkReason reason)
{
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        scheduler,
        {
            for_each_fa(
                ThreadTable,
                scheduler->threads,
                thread_index,
                thread,
                {
                    if (thread->owner == proc &&
                        Thread__has_parked_operation(thread, reason, &None(OperationKey)))
                    {
                        return true;
                    }
                }
            );
        }
    );

    return false;
}

static ProcessTimes Scheduler__process_times_at(ProcessPtr process, CPUTime now)
{
    ProcessCPUUsage usage =
        CPUUsage__snapshot(&process->accounting.cpu_usage, now, Process__user_wait_usec(process, now));
    ProcessTimes times = ProcessTimes__new();
    times.elapsed_usec = now;
    times.user_usec = usage.user_usec;
    times.system_usec = usage.system_usec;
    times.children_user_usec = process->accounting.children_user_usec;
    times.children_system_usec = process->accounting.children_system_usec;
    return times;
}

ProcessTimes Scheduler__process_times(SchedulerPtr self, ProcessPtr process)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Scheduler__process_times_at(process, CPU__time_usec()));
        }
    );
}

static Result(ProcessID) Scheduler__terminate_process(SchedulerPtr self, ProcessPtr proc, ReturnCode return_code)
{
    match_optional(
        self->kernel,
        some_do_nothing(),
        none()
        {
            return Error(ProcessID, bstring("Scheduler has no owning kernel"), SCHEDULER_GENERAL_ERROR);
        }
    );
    ProcessID pid = proc->pid;
    critical_section(
        self->scheduler_lock,
        {
            if (is_nothing(Scheduler__scheduler_owning_process_locked(self, proc)))
            {
                cs_return(Error(ProcessID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND));
            }
            if (proc->lifecycle.terminating)
            {
                cs_return(
                    Error(ProcessID, bstring("Process already terminating"), SCHEDULER_GENERAL_ERROR));
            }
            proc->lifecycle.terminating = true;
        }
    );
    bool remote_active = true;
    while (remote_active)
    {
        remote_active = false;
        for_each_hashtable(
            CPUSchedulers,
            self->schedulers,
            bucket_index,
            entry_index,
            cpu,
            sched,
            {
                if (cpu == CPU__id())
                {
                    continue;
                }
                bool uses_process = false;
                critical_section(
                    self->scheduler_lock,
                    {
                        match_optional(
                            sched->active_process,
                            some(active)
                            {
                                uses_process = active == proc;
                            },
                            none_do_nothing()
                        );
                        match_optional(
                            sched->dispatching_process,
                            some(dispatching)
                            {
                                uses_process = uses_process || dispatching == proc;
                            },
                            none_do_nothing()
                        );
                    }
                );
                if (uses_process)
                {
                    remote_active = true;
                    CPU__request_reschedule(cpu);
                }
            }
        );
        if (remote_active)
        {
            CPU__yield();
        }
    }
    match_result(
        Process__dispose_owned_kernel_resources(proc),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessID, message, SCHEDULER_GENERAL_ERROR);
        }
    );
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        sched,
        {
            Result(ProcessPtr) res = Scheduler__detach_exiting_process(self, sched, proc, return_code);
            match_result(
                res,
                success(exited)
                {
                    MMU__dereference_user_mappings(proc->kernel,
                                                   ProcessPageTable__root(&proc->address_space.page_table));
                    critical_section(
                        self->scheduler_lock,
                        {
                            match_optional(
                                CPUSchedulers__get(&self->schedulers, CPU__id()),
                                some(local)
                                {
                                    match_optional(
                                        local->active_process,
                                        some(active)
                                        {
                                            if (active == proc)
                                            {
                                                local->active_process = None(ProcessPtr);
                                            }
                                        },
                                        none_do_nothing()
                                    );
                                    match_optional(
                                        local->dispatching_process,
                                        some(dispatching)
                                        {
                                            if (dispatching == proc)
                                            {
                                                local->dispatching_process = None(ProcessPtr);
                                            }
                                        },
                                        none_do_nothing()
                                    );
                                },
                                none_do_nothing()
                            );
                        }
                    );
                    match_result(
                        Process__dispose_user_allocations(proc),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(ProcessID, message, SCHEDULER_GENERAL_ERROR);
                        }
                    );
                    match_result(
                        Process__dispose_page_table(proc),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(ProcessID, message, SCHEDULER_GENERAL_ERROR);
                        }
                    );
                    match_result(
                        Process__dispose(proc),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(ProcessID, message, SCHEDULER_GENERAL_ERROR);
                        }
                    );

                    Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                    match_optional(
                        current_for_cpu,
                        some_do_nothing(),
                        none()
                        {
                            Scheduler__preempt(self, cpu, &None(ThreadContextPtr));
                        }
                    );

                    return Success(ProcessID, pid);
                },
                error(code, message)
                {
                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                    {
                        return Error(ProcessID, message, code);
                    }
                }
            );
        }
    );

    BString no_scheduler = BString__sprintf(&bstr("Process not found: %i\n"), pid);
    return Error(ProcessID, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

void Scheduler__address_space_switched(SchedulerPtr self, CPUID cpu, ProcessPtr process)
{
    bool terminating = false;
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    scheduler->active_process = Some(ProcessPtr, process);
                    scheduler->dispatching_process = None(ProcessPtr);
                },
                none_do_nothing()
            );
            terminating = process->lifecycle.terminating;
        }
    );

    if (terminating)
    {
        CPU__request_reschedule(cpu);
    }
}

void Scheduler__revoke_current_if_terminating(SchedulerPtr self, CPUID cpu, ThreadContextPtr context)
{
    bool terminating = false;
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    match_optional(
                        CPUScheduler__current_thread(scheduler),
                        some(thread)
                        {
                            terminating = thread->owner->lifecycle.terminating;
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );
    if (terminating)
    {
        Scheduler__preempt(self, cpu, &Some(ThreadContextPtr, context));
    }
}

static Result(ServiceCode) Scheduler__notify_parent_child_exit_locked(SchedulerPtr self, ProcessPtr parent,
                                                                      ProcessPtr child, ReturnCode return_code)
{
    ProcessTimes child_times = Scheduler__process_times_at(child, CPU__time_usec());
    ProcessChildEvent event = ProcessChildEvent__new();
    event.pid = child->pid;
    event.process_group_id = child->process_group_id;
    if (return_code <= PROCESS_SIGNAL_EXIT_BASE - 1 && return_code >= PROCESS_SIGNAL_EXIT_BASE - 127)
    {
        event.status = PROCESS_WAIT_STATUS_SIGNALED(PROCESS_SIGNAL_EXIT_BASE - return_code);
    }

    else if (return_code <= PROCESS_ABORT_EXIT_BASE - 1 && return_code >= PROCESS_ABORT_EXIT_BASE - 127)
    {
        event.status = PROCESS_WAIT_STATUS_SIGNALED(PROCESS_WAIT_ABORT_SIGNAL);
    }

    else
    {
        event.status = PROCESS_WAIT_STATUS_EXITED(return_code);
    }

    event.type = PROCESS_CHILD_EVENT_EXITED;
    event.user_usec = child_times.user_usec + child_times.children_user_usec;
    event.system_usec = child_times.system_usec + child_times.children_system_usec;

    ProcessSignalAction action = Process__signal_action(parent, PROCESS_SIGNAL_CHILD);
    bool retains_child_event =
        (action.flags & PROCESS_SIGNAL_ACTION_NOCLDWAIT) == 0 && action.handler != PROCESS_SIGNAL_IGNORE_HANDLER;
    Scheduler__trace_child_exit(parent, child, &event, retains_child_event);
    if (retains_child_event)
    {
        match_result(
            Process__push_child_event(parent, &event),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ServiceCode, message, code);
            }
        );
        match_result(
            Scheduler__child_mark_runnable_locked(self, parent, child->pid, child->process_group_id, event.type),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ServiceCode, message, code);
            }
        );
    }

    Process__queue_signal(parent, PROCESS_SIGNAL_CHILD);
    if (!retains_child_event)
    {
        Process__wake_shared_wait(parent);
    }

    return Success(ServiceCode, SERVICE_OK);
}

static Result(ServiceCode)
    Scheduler__notify_parent_child_state_locked(SchedulerPtr self, ProcessPtr parent, ProcessPtr child,
                                                uint64_t event_type, ProcessSignal signal)
{
    ProcessChildEvent event = ProcessChildEvent__new();
    event.pid = child->pid;
    event.process_group_id = child->process_group_id;
    event.type = event_type;
    event.status =
        event_type == PROCESS_CHILD_EVENT_STOPPED ? PROCESS_WAIT_STATUS_STOPPED(signal) : PROCESS_WAIT_STATUS_CONTINUED;
    match_result(
        Process__push_child_event(parent, &event),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    match_result(
        Scheduler__child_mark_runnable_locked(self, parent, child->pid, child->process_group_id, event.type),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );

    ProcessSignalAction action = Process__signal_action(parent, PROCESS_SIGNAL_CHILD);
    if ((action.flags & PROCESS_SIGNAL_ACTION_NOCLDSTOP) == 0)
    {
        Process__queue_signal(parent, PROCESS_SIGNAL_CHILD);
    }

    return Success(ServiceCode, SERVICE_OK);
}

Result(ProcessID) Scheduler__exit(SchedulerPtr self, ProcessPtr proc, ReturnCode return_code)
{
    return Scheduler__terminate_process(self, proc, return_code);
}

Result(ProcessID) Scheduler__abort_process(SchedulerPtr self, ProcessPtr proc, ProcessAbort abort)
{
    proc->lifecycle.abort = Some(ProcessAbort, abort);
    ConstBStringPtr reason = ProcessAbortReason__to_string(abort.reason);
    BString message =
        BString__sprintf(&abort_format, proc->pid, cstr(proc->name), cstr_p(reason), abort.exception_description,
                         (int)abort.fault_status_code, abort.fault_status_description, abort.access_description,
                         (unsigned long long)abort.pc, (unsigned long long)abort.fault_address,
                         (unsigned long long)abort.syndrome, (unsigned long long)abort.exception_code, (int)abort.cpu);
    k_printf("%s", BString__cstr(&message));
    ReturnCode return_code = PROCESS_ABORT_EXIT_BASE - abort.reason;
    return Scheduler__terminate_process(self, proc, return_code);
}

Result(ProcessPtr) Scheduler__find_process(SchedulerPtr self, ProcessID pid)
{
    return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

bool pid_matches(ProcessPtr * proc_p, void * pid_p)
{
    ProcessPtr proc = *proc_p;
    ProcessID pid = *(ProcessIDPtr)pid_p;

    __d_printf(SCHEDULER, "pid_matches %i %i\n", proc->pid, pid);
    return proc->pid == pid;
}

Optional(ProcessPtr) Scheduler__find_process_by_pid_locked(SchedulerPtr self, ProcessID pid)
{
    ProcessTable processes = Scheduler__all_processes_for_all_cpus_locked(self);
    __d_printf(SCHEDULER, "Scheduler__find_process_by_pid %i\n", ProcessTable__len(&processes));
    match_find_result(
        ProcessTable__find(&processes, pid_matches, &pid),
        some(process)
        {
            return Some(ProcessPtr, process);
        },
        none()
        {
            return None(ProcessPtr);
        }
    );
}

Optional(ProcessPtr) Scheduler__find_process_by_pid(SchedulerPtr self, ProcessID pid)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Scheduler__find_process_by_pid_locked(self, pid));
        }
    );

    return None(ProcessPtr);
}

Result(ProcessCredentials) Scheduler__process_credentials(SchedulerPtr self, ProcessID pid)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Scheduler__find_process_by_pid_locked(self, pid),
                some(process)
                {
                    cs_return(Success(ProcessCredentials,
                                      ProcessCredentials__new(process->user_id, process->group_id)));
                },
                none_do_nothing()
            );
        }
    );

    return Error(ProcessCredentials, process_not_found, PROCESS_CREDENTIALS_NOT_FOUND);
}

typedef struct ProcessGroupSelection
{
    ProcessGroupID process_group_id;
    ProcessSessionID session_id;
} ProcessGroupSelection;

typedef ProcessGroupSelection * ProcessGroupSelectionPtr;

static ProcessGroupSelection ProcessGroupSelection__new(ProcessGroupID process_group_id, ProcessSessionID session_id)
{
    return (ProcessGroupSelection){
        .process_group_id = process_group_id,
        .session_id = session_id,
    };
}

static bool Scheduler__matches_process_group(ProcessPtr * candidate, void * context)
{
    ProcessGroupSelectionPtr selection = context;
    return (*candidate)->process_group_id == selection->process_group_id &&
           (*candidate)->session_id == selection->session_id;
}

static bool Scheduler__caller_can_set_process_group(ProcessPtr caller, ProcessPtr target)
{
    if (caller == target)
    {
        return true;
    }

    match_optional(
        target->parent,
        some(parent)
        {
            return parent == caller && !target->lifecycle.execed_since_fork;
        },
        none_do_nothing()
    );
    return false;
}

Result(ProcessGroupID) Scheduler__process_group(SchedulerPtr self, ProcessPtr caller, int64_t pid)
{
    if (pid < 0)
    {
        return Error(ProcessGroupID, bstr("Invalid process group query"), PROCESS_GROUP_INVALID_ARGUMENT);
    }

    ProcessID target_pid = pid == 0 ? caller->pid : (ProcessID)pid;
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Scheduler__find_process_by_pid_locked(self, target_pid),
                some(target)
                {
                    cs_return(Success(ProcessGroupID, target->process_group_id));
                },
                none_do_nothing()
            );
        }
    );

    return Error(ProcessGroupID, process_not_found, PROCESS_GROUP_PROCESS_NOT_FOUND);
}

Result(ProcessGroupID)
    Scheduler__set_process_group(SchedulerPtr self, ProcessPtr caller, int64_t pid, int64_t process_group_id)
{
    if (pid < 0 || process_group_id < 0)
    {
        return Error(ProcessGroupID, bstr("Invalid process group change"), PROCESS_GROUP_INVALID_ARGUMENT);
    }

    ProcessID target_pid = pid == 0 ? caller->pid : (ProcessID)pid;
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Scheduler__find_process_by_pid_locked(self, target_pid),
                some(target)
                {
                    ProcessGroupID requested = process_group_id == 0 ? target->pid : (ProcessGroupID)process_group_id;
                    if (!Scheduler__caller_can_set_process_group(caller, target) ||
                        target->session_id != caller->session_id || target->session_id == target->pid)
                    {
                        cs_return(Error(ProcessGroupID, process_permission_denied, PROCESS_GROUP_PERMISSION_DENIED));
                    }
                    if (requested != target->pid)
                    {
                        ProcessGroupSelection selection = ProcessGroupSelection__new(requested, caller->session_id);
                        ProcessTable processes = Scheduler__all_processes_for_all_cpus_locked(self);
                        bool group_exists = false;
                        match_find_result(
                            ProcessTable__find(&processes, Scheduler__matches_process_group, &selection),
                            some(member)
                            {
                                group_exists = true;
                            },
                            none_do_nothing()
                        );
                        if (!group_exists)
                        {
                            cs_return(
                                Error(ProcessGroupID, process_permission_denied, PROCESS_GROUP_PERMISSION_DENIED));
                        }
                    }
                    target->process_group_id = requested;
                    cs_return(Success(ProcessGroupID, requested));
                },
                none_do_nothing()
            );
        }
    );

    return Error(ProcessGroupID, process_not_found, PROCESS_GROUP_PROCESS_NOT_FOUND);
}

Result(ProcessSessionID) Scheduler__create_session(SchedulerPtr self, ProcessPtr caller)
{
    critical_section(
        self->scheduler_lock,
        {
            if (caller->process_group_id == caller->pid)
            {
                cs_return(
                    Error(ProcessSessionID, process_permission_denied, PROCESS_GROUP_PERMISSION_DENIED));
            }
            caller->session_id = caller->pid;
            caller->process_group_id = caller->pid;
            cs_return(Success(ProcessSessionID, caller->session_id));
        }
    );
    return Error(ProcessSessionID, process_permission_denied, PROCESS_GROUP_PERMISSION_DENIED);
}

Result(ServiceCode)
    Scheduler__require_process_capability(SchedulerPtr self, ProcessID pid, ProcessCapabilities capability)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Scheduler__find_process_by_pid_locked(self, pid),
                some(process)
                {
                    if (Process__has_capability(process, capability))
                    {
                        cs_return(Success(ServiceCode, SERVICE_OK));
                    }
                    cs_return(Error(ServiceCode, process_permission_denied, SERVICE_PERMISSION_DENIED));
                },
                none_do_nothing()
            );
        }
    );

    return Error(ServiceCode, process_not_found, SERVICE_PERMISSION_DENIED);
}

static Result(ProcessID)
    Scheduler__queue_signal_to_process_locked(SchedulerPtr self, ProcessPtr process, ProcessSignal signal)
{
    if (signal != 0 && !Process__supports_signal(process, signal))
    {
        return Error(ProcessID, bstring("Unsupported signal"), PROCESS_SIGNAL_UNSUPPORTED);
    }

    if (signal == 0)
    {
        return Success(ProcessID, process->pid);
    }

    // Only the owning CPU may destroy a target that a context switch can still reference.
    Process__queue_signal(process, signal);
    Process__wake_shared_wait(process);
    bool signal_wait_ready = false;
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        scheduler,
        {
            for_each_fa(
                ThreadTable,
                scheduler->threads,
                thread_index,
                thread,
                {
                    if (!signal_wait_ready && thread->owner == process &&
                        Thread__mark_signal_suspend_ready(thread))
                    {
                        thread->state = PROC_STATE_INTERRUPTED;
                        signal_wait_ready = true;
                    }
                }
            );
        }
    );

    if (process->thread.state == PROC_STATE_STOPPED && signal == PROCESS_SIGNAL_CONTINUE)
    {
        process->thread.state = PROC_STATE_INTERRUPTED;
        match_optional(
            process->parent,
            some(parent)
            {
                match_result(
                    Scheduler__notify_parent_child_state_locked(self, parent, process, PROCESS_CHILD_EVENT_CONTINUED,
                                                                signal),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return Error(ProcessID, message, code);
                    }
                );
            },
            none_do_nothing()
        );
    }
    else if (process->thread.state == PROC_STATE_STOPPED && signal == PROCESS_SIGNAL_KILL)
    {
        process->thread.state = PROC_STATE_INTERRUPTED;
    }

    return Success(ProcessID, process->pid);
}

Result(ProcessID) Scheduler__queue_signal(SchedulerPtr self, ProcessID pid, ProcessSignal signal)
{
    if (pid == 0)
    {
        return Error(ProcessID, bstring("Invalid signal target"), PROCESS_SIGNAL_INVALID_ARGUMENT);
    }

    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Scheduler__find_process_by_pid_locked(self, pid),
                some(process)
                {
                    cs_return(Scheduler__queue_signal_to_process_locked(self, process, signal));
                },
                none_do_nothing()
            );
        }
    );

    return Error(ProcessID, bstring("Process not found"), PROCESS_SIGNAL_PROCESS_NOT_FOUND);
}

static bool Scheduler__signal_target_matches(ProcessPtr sender, ProcessPtr candidate, int64_t target)
{
    if (target > 0)
    {
        return candidate->pid == (ProcessID)target;
    }

    if (target == 0)
    {
        return candidate->process_group_id == sender->process_group_id;
    }

    if (target == -1)
    {
        return candidate != sender;
    }

    return target != INT64_MIN && candidate->process_group_id == (ProcessGroupID)-target;
}

static bool Scheduler__signal_permitted(ProcessPtr sender, ProcessPtr target, ProcessSignal signal)
{
    return Process__is_root(sender) || sender->user_id == target->user_id ||
           (signal == PROCESS_SIGNAL_CONTINUE && sender->session_id == target->session_id);
}

static Result(ProcessID) Scheduler__send_signal_authorized(SchedulerPtr self, ProcessPtr sender, int64_t target,
                                                           ProcessSignal signal, bool terminal_interrupt)
{
    if (target == INT64_MIN || signal > PROCESS_SIGNAL_COUNT)
    {
        return Error(ProcessID, bstr("Invalid signal request"), PROCESS_SIGNAL_INVALID_ARGUMENT);
    }

    critical_section(
        self->scheduler_lock,
        {
            ProcessTable processes = Scheduler__all_processes_for_all_cpus_locked(self);
            uint64_t matched = 0;
            uint64_t delivered = 0;
            for_each_fa(
                ProcessTable,
                processes,
                process_index,
                candidate,
                {
                    if (Scheduler__signal_target_matches(sender, candidate, target))
                    {
                        matched++;
                        if (terminal_interrupt || Scheduler__signal_permitted(sender, candidate, signal))
                        {
                            match_result(
                                Scheduler__queue_signal_to_process_locked(self, candidate, signal),
                                success(pid)
                                {
                                    delivered++;
                                },
                                error(code, message)
                                {
                                    cs_return(Error(ProcessID, message, code));
                                }
                            );
                        }
                    }
                }
            );
            if (delivered > 0)
            {
                cs_return(Success(ProcessID, delivered));
            }
            if (matched > 0)
            {
                cs_return(Error(ProcessID, process_permission_denied, PROCESS_SIGNAL_PERMISSION_DENIED));
            }
        }
    );

    return Error(ProcessID, process_not_found, PROCESS_SIGNAL_PROCESS_NOT_FOUND);
}

Result(ProcessID) Scheduler__send_signal(SchedulerPtr self, ProcessPtr sender, int64_t target, ProcessSignal signal)
{
    return Scheduler__send_signal_authorized(self, sender, target, signal, false);
}

Result(ProcessID) Scheduler__send_terminal_interrupt(SchedulerPtr self, ProcessPtr sender, int64_t target)
{
    if (target >= -1 || target == INT64_MIN)
    {
        return Error(ProcessID, bstr("Invalid terminal signal target"), PROCESS_SIGNAL_INVALID_ARGUMENT);
    }
    if (!Process__has_capability(sender, PROCESS_CAP_SIGNAL_TERMINAL))
    {
        return Error(ProcessID, process_permission_denied, PROCESS_SIGNAL_PERMISSION_DENIED);
    }

    return Scheduler__send_signal_authorized(self, sender, target, PROCESS_SIGNAL_INTERRUPT, true);
}

ProcessSignalDeliveryKind Scheduler__deliver_pending_signal(SchedulerPtr self, ThreadPtr thread,
                                                            ThreadContextPtr context)
{
    ProcessPtr process = thread->owner;
    ProcessSignalDelivery delivery = ProcessSignalDelivery__new(PROCESS_SIGNAL_DELIVERY_NONE, 0);
    match_result(
        Thread__deliver_pending_signal(thread, context),
        success(prepared)
        {
            delivery = prepared;
        },
        error(code, message)
        {
            delivery.kind = PROCESS_SIGNAL_DELIVERY_TERMINATE;
            delivery.signal = PROCESS_SIGNAL_KILL;
        }
    );

    match_optional(
        delivery.interrupted_operation,
        some(operation)
        {
            if (operation->park_reason == PARK_WAIT_QUEUE)
            {
                Thread__clear_kernel_queue_wait(thread);
                QueueWaitUsage__cancel(&thread->queue_wait_usage);
            }
            ParkedOperation__dispose(operation);
        },
        none_do_nothing()
    );

    if (delivery.kind == PROCESS_SIGNAL_DELIVERY_TERMINATE)
    {
        match_result(
            Scheduler__exit(self, process, ProcessSignal__exit_code(delivery.signal)),
            success_do_nothing(),
            error(code, message)
            {
                Kernel__panic(cstr(message), context);
            }
        );
    }

    else if (delivery.kind == PROCESS_SIGNAL_DELIVERY_STOP && thread->state != PROC_STATE_STOPPED)
    {
        match_result(
            Thread__store_context(thread, context),
            success_do_nothing(),
            error(code, message)
            {
                Kernel__panic(cstr(message), context);
            }
        );
        critical_section(
            self->scheduler_lock,
            {
                thread->state = PROC_STATE_STOPPED;
                match_optional(
                    process->parent,
                    some(parent)
                    {
                        match_result(
                            Scheduler__notify_parent_child_state_locked(
                                self, parent, process, PROCESS_CHILD_EVENT_STOPPED, delivery.signal),
                            success_do_nothing(),
                            error(code, message)
                            {
                                Kernel__panic(cstr(message), context);
                            }
                        );
                    },
                    none_do_nothing()
                );
            }
        );
    }

    return delivery.kind;
}

Result(ProcessID) Scheduler__park_by_pid(SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
                                         OptionalPtr(Continuation) kernel_continuation, ParkReason reason)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return Scheduler__park_by_pid_with_context_ownership_and_operation_key(
        self, pid, kernel_context, kernel_continuation, reason, false, &None(OperationKey));
}

Result(ProcessID)
    Scheduler__park_by_pid_with_context_ownership(SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
                                                  OptionalPtr(Continuation) kernel_continuation, ParkReason reason,
                                                  bool owns_kernel_context)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return Scheduler__park_by_pid_with_context_ownership_and_operation_key(
        self, pid, kernel_context, kernel_continuation, reason, owns_kernel_context, &None(OperationKey));
}

Result(ProcessID) Scheduler__park_by_pid_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context, OptionalPtr(Continuation) kernel_continuation,
    ParkReason reason, bool owns_kernel_context, OptionalPtr(OperationKey) operation_key)
{
    match_optional(
        Scheduler__find_process_by_pid(self, pid),
        some(proc)
        {
            match_result(
                Scheduler__park_with_context_ownership_and_operation_key(
                    self, proc, kernel_context, kernel_continuation, reason, owns_kernel_context, operation_key, 0),
                success(proc)
                {
                    return Success(ProcessID, pid);
                },
                error(code, message)
                {
                    return Error(ProcessID, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ProcessID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessPtr) Scheduler__park(SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                                   OptionalPtr(Continuation) kernel_continuation, ParkReason reason)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return Scheduler__park_with_context_ownership_and_operation_key(self, proc, kernel_context, kernel_continuation,
                                                                    reason, false, &None(OperationKey), 0);
}

Result(ProcessPtr)
    Scheduler__park_with_context_ownership(SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                                           OptionalPtr(Continuation) kernel_continuation, ParkReason reason,
                                           bool owns_kernel_context)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return Scheduler__park_with_context_ownership_and_operation_key(
        self, proc, kernel_context, kernel_continuation, reason, owns_kernel_context, &None(OperationKey), 0);
}

Result(ProcessPtr) Scheduler__park_wait_child(SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
                                              OptionalPtr(Continuation) kernel_continuation,
                                              OptionalPtr(KernelAddress) abort_report, bool owns_kernel_context)
{
    Anything operation_context = 0;
    match_optional(*abort_report,
        some(address)
        {
            operation_context = address.virtual_addr;
        },
        none_do_nothing()
    );
    return Scheduler__park_with_context_ownership_and_operation_key(self, proc, kernel_context, kernel_continuation,
                                                                    PARK_WAIT_CHILD, owns_kernel_context,
                                                                    &None(OperationKey), operation_context);
}

static Result(ProcessPtr) Scheduler__park_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context, OptionalPtr(Continuation) kernel_continuation,
    ParkReason reason, bool owns_kernel_context, OptionalPtr(OperationKey) operation_key, Anything operation_context)
{
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        sched,
        {
            Result(ProcessPtr) res =
                CPUScheduler__park(sched, proc, kernel_context, kernel_continuation, reason,
                                   owns_kernel_context, operation_key, operation_context);
            match_result(
                res,
                success(exited)
                {
                    Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                    match_optional(
                        current_for_cpu,
                        some_do_nothing(),
                        none()
                        {
                            Scheduler__preempt(self, cpu, &None(ThreadContextPtr));
                        }
                    );

                    return res;
                },
                error(code, message)
                {
                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                    {
                        return Error(ProcessPtr, message, code);
                    }
                }
            );
        }
    );

    BString no_scheduler = BString__sprintf(&bstr("No scheduler found for process: %i\n"), proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessID)
    Scheduler__unpark_by_pid(SchedulerPtr self, ProcessID pid, ParkReason reason, OptionalPtr(ReturnCode) return_code)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return Scheduler__unpark_by_pid_with_operation_key(self, pid, reason, return_code, &None(OperationKey));
}

Result(ProcessID) Scheduler__unpark_by_pid_with_operation_key(SchedulerPtr self, ProcessID pid, ParkReason reason,
                                                              OptionalPtr(ReturnCode) return_code,
                                                              OptionalPtr(OperationKey) operation_key)
{
    match_optional(
        Scheduler__find_process_by_pid(self, pid),
        some(proc)
        {
            match_result(
                Scheduler__unpark_with_operation_key(self, proc, reason, return_code, operation_key),
                success(proc)
                {
                    return Success(ProcessID, pid);
                },
                error(code, message)
                {
                    return Error(ProcessID, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ProcessID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessPtr)
    Scheduler__unpark(SchedulerPtr self, ProcessPtr proc, ParkReason reason, OptionalPtr(ReturnCode) return_code)
{
    Optional(OperationKey) operation_key = None(OperationKey);
    return Scheduler__unpark_with_operation_key(self, proc, reason, return_code, &None(OperationKey));
}

Result(ThreadWaitStatus) Scheduler__wait_atomic(SchedulerPtr self, ThreadPtr caller, Address address,
                                                ThreadWaitValue expected, ThreadWaitTimeout timeout_usec)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Process__atomic_wait_mapping(caller->owner, address),
                some(mapping)
                {
                    ThreadWaitValuePtr word = (ThreadWaitValuePtr)mapping.virtual_addr;
                    if (__atomic_load_n(word, __ATOMIC_ACQUIRE) != expected)
                    {
                        cs_return(Success(ThreadWaitStatus, THREAD_WAIT_CHANGED));
                    }
                },
                none()
                {
                    cs_return(Error(ThreadWaitStatus, bstr("Invalid atomic wait address"), THREAD_WAIT_INVALID));
                }
            );
            if (Thread__can_resume_for_signal(caller))
            {
                cs_return(Success(ThreadWaitStatus, THREAD_WAIT_INTERRUPTED));
            }
            if (timeout_usec == 0)
            {
                cs_return(Success(ThreadWaitStatus, THREAD_WAIT_TIMED_OUT));
            }
            match_optional(
                CPUSchedulers__get(&self->schedulers, CPU__id()),
                some(cpu)
                {
                    if (is_nothing(ThreadTable__find_item(&cpu->threads, caller)))
                    {
                        cs_return(Error(ThreadWaitStatus, process_not_found, THREAD_WAIT_INVALID));
                    }
                    Optional(OperationKey) deadline = None(OperationKey);
                    if (timeout_usec != THREAD_WAIT_INDEFINITE)
                    {
                        Tick ticks = CPUScheduler__sleep_ticks_from_usec(timeout_usec);
                        if (ticks >= UINT64_MAX - cpu->last_tick)
                        {
                            cs_return(
                                Error(ThreadWaitStatus, bstr("Atomic wait deadline overflow"), THREAD_WAIT_INVALID));
                        }
                        deadline = Some(OperationKey, cpu->last_tick + ticks + 1);
                    }
                    KernelContextPtr context =
                        KernelContext__heap_new(&Some(ThreadPtr, caller), &Some(SchedulerPtr, self));
                    match_result(
                        CPUScheduler__park(cpu, caller->owner, context, &Some(Continuation, SystemCall__Complete),
                                           PARK_WAIT_ATOMIC, true, &deadline, address),
                        success(_)
                        {
                            cs_return(Error(ThreadWaitStatus, bstr("Atomic wait pending"), SYSCALL_IO_BLOCK));
                        },
                        error(code, message)
                        {
                            KernelContext__dispose_heap(context);
                            cs_return(Error(ThreadWaitStatus, message, THREAD_WAIT_FAILED));
                        }
                    );
                },
                none_do_nothing()
            );
        }
    );
    return Error(ThreadWaitStatus, process_not_found, THREAD_WAIT_INVALID);
}

Result(ThreadWakeResult) Scheduler__wake_atomic(SchedulerPtr self, ThreadPtr caller, Address address, uint64_t count)
{
    critical_section(
        self->scheduler_lock,
        {
            if (is_nothing(Process__atomic_wait_mapping(caller->owner, address)))
            {
                cs_return(
                    Error(ThreadWakeResult, bstr("Invalid atomic wake address"), THREAD_WAIT_INVALID));
            }
            ThreadWakeResult woken = 0;
            for_each_ll(
                ProcessThreads,
                caller->owner->threads,
                thread,
                {
                    if ((uint64_t)woken < count && Thread__wake_atomic_wait(thread, address))
                    {
                        woken++;
                    }
                }
            );
            cs_return(Success(ThreadWakeResult, woken));
        }
    );
    return Error(ThreadWakeResult, process_not_found, THREAD_WAIT_INVALID);
}

Result(ProcessPtr) Scheduler__sleep_usec(SchedulerPtr self, ThreadPtr thread, uint64_t usec)
{
    ProcessPtr process = thread->owner;
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        sched,
        {
            match_optional(
                ThreadTable__find_item(&sched->threads, thread),
                some(index)
                {
                    Tick deadline = sched->last_tick + CPUScheduler__sleep_ticks_from_usec(usec) + 1ULL;
                    Optional(SchedulerPtr) scheduler = Some(SchedulerPtr, self);
                    KernelContextPtr kernel_context = KernelContext__heap_new(&Some(ThreadPtr, thread), &scheduler);

                    Optional(Continuation) continuation = Some(Continuation, SystemCall__Complete);
                    Optional(OperationKey) operation_key = Some(OperationKey, deadline);
                    return Scheduler__park_with_context_ownership_and_operation_key(
                        self, process, kernel_context, &continuation, PARK_SLEEP, true, &operation_key, 0);
                },
                none_do_nothing()
            );
        }
    );

    BString no_scheduler = BString__sprintf(&bstr("No scheduler found for process: %i\n"), process->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessPtr) Scheduler__signal_suspend(SchedulerPtr self, ThreadPtr thread, ProcessSignalMask mask)
{
    ProcessPtr process = thread->owner;
    ProcessSignalMask previous_mask = Thread__signal_mask(thread);
    match_result(
        Thread__set_signal_mask(thread, PROCESS_SIGNAL_MASK_SET, mask),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ProcessPtr, message, code);
        }
    );

    critical_section(
        self->scheduler_lock,
        {
            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                sched,
                {
                    if (is_something(ThreadTable__find_item(&sched->threads, thread)))
                    {
                        KernelContextPtr context =
                            KernelContext__heap_new(&Some(ThreadPtr, thread), &Some(SchedulerPtr, self));
                        match_result(
                            CPUScheduler__park(sched, process, context,
                                               &Some(Continuation, SystemCall__Complete), PARK_WAIT_SIGNAL,
                                               true, &None(OperationKey), previous_mask),
                            success(parked)
                            {
                                if (Thread__mark_signal_suspend_ready(thread))
                                {
                                    thread->state = PROC_STATE_INTERRUPTED;
                                }
                                cs_return(Success(ProcessPtr, parked));
                            },
                            error(code, message)
                            {
                                Thread__set_signal_mask(thread, PROCESS_SIGNAL_MASK_SET, previous_mask);
                                cs_return(Error(ProcessPtr, message, code));
                            }
                        );
                    }
                }
            );
        }
    );

    Thread__set_signal_mask(thread, PROCESS_SIGNAL_MASK_SET, previous_mask);
    return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(ProcessPtr)
    Scheduler__unpark_with_operation_key(SchedulerPtr self, ProcessPtr proc, ParkReason reason,
                                         OptionalPtr(ReturnCode) return_code, OptionalPtr(OperationKey) operation_key)
{
    __d_println(SCHEDULER, "scheduler unparking");
    bool found_process = false;
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        sched,
        {
            Result(ProcessPtr) res =
                CPUScheduler__unpark_with_operation_key(sched, proc, reason, return_code, operation_key);
            match_result(
                res,
                success(unparked)
                {
                    __d_println(SCHEDULER, "scheduler unparked process");
                    return res;
                },
                error(code, message)
                {
                    if (code == SCHEDULER_CANT_UNPARK)
                    {
                        found_process = true;
                    }
                    else if (code != SCHEDULER_PROCESS_NOT_FOUND)
                    {
                        return Error(ProcessPtr, message, code);
                    }
                }
            );
        }
    );

    if (found_process)
    {
        return Error(ProcessPtr, process_wasnt_parked, SCHEDULER_CANT_UNPARK);
    }

    BString no_scheduler = BString__sprintf(&bstr("Process not found: %i\n"), proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(CollectionCode) Scheduler__add_blocked_operation(SchedulerPtr self, ParkedOperationPtr operation,
                                                        ParkedOperationPtr blocked_operation)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                ParkedOperation__add_blocked_operation(operation, blocked_operation),
                success(index)
                {
                    cs_return(Success(CollectionCode, index));
                },
                error(code, message)
                {
                    cs_return(Error(CollectionCode, message, code));
                }
            );
        }
    );

    return Error(CollectionCode, bstring("Unable to add blocked operation"), LINKED_LIST_ALLOCATION_FAILED);
}

Result(CollectionCode) Scheduler__resolve_blocked_operation(SchedulerPtr self, ParkedOperationPtr operation,
                                                            ParkedOperationPtr blocked_operation)
{
    Result(CollectionCode) resolved_operation =
        Error(CollectionCode, bstring("Unable to resolve blocked operation"), LINKED_LIST_ITEM_NOT_FOUND);

    critical_section(
        self->scheduler_lock,
        {
            resolved_operation = ParkedOperation__resolve_blocked_operation(operation, blocked_operation);
        }
    );

    match_result(
        resolved_operation,
        success(index)
        {
            ParkedOperation__dispose(blocked_operation);
            return Success(CollectionCode, index);
        },
        error(code, message)
        {
            return Error(CollectionCode, message, code);
        }
    );

    return Error(CollectionCode, bstring("Unable to resolve blocked operation"), LINKED_LIST_ITEM_NOT_FOUND);
}

bool Scheduler__parked_operation_is_ready(SchedulerPtr self, ParkedOperationPtr operation)
{
    bool ready = false;
    critical_section(
        self->scheduler_lock,
        ready = ParkedOperation__is_ready(operation);
    )
    ;
    return ready;
}

Result(SchedulerInitStatus) Scheduler__init(KernelPtr kernel, SchedulerPtr self, Elf idle_process)
{
    __d_println(SCHEDULER, "Scheduler__init");
    self->kernel = Some(KernelPtr, kernel);
    self->request_graph = RequestGraph__new();
    self->preferred = ProcessTable__new();
    self->admission = ProcessTable__new();
    self->schedulers = CPUSchedulers__new();
    self->idle_program_start = idle_process.start;
    self->idle_program_end = idle_process.end;
    self->idle_program_name = idle_process.name;
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            self->interrupt_controller = Some(InterruptControllerInfo, system_info.interrupt_controller);
        },
        none_do_nothing()
    );
    self->ready = false;
    return Success(SchedulerInitStatus, SCHEDULER_INIT_OK);
}

Result(SchedulerInitStatus) Scheduler__register_cpu(SchedulerPtr self, CPUID cpu)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(existing)
                {
                    cs_return(Success(SchedulerInitStatus, SCHEDULER_INIT_OK));
                },
                none_do_nothing()
            );
            if (CPUSchedulers__len(&self->schedulers) >= MAX_CPUS)
            {
                cs_return(Error(SchedulerInitStatus, no_cpus_available, SCHEDULER_INIT_ERROR));
            }
            match_optional(
                self->kernel,
                some(kernel)
                {
                    Elf idle_program =
                        Elf__new(self->idle_program_start, self->idle_program_end, &self->idle_program_name);
                    match_result(
                        CPUScheduler__heap_new(kernel, idle_program, cpu),
                        success(cpu_scheduler)
                        {
                            cpu_scheduler->registry_slot = CPUSchedulers__len(&self->schedulers);
                            match_result(
                                CPUSchedulers__set(&self->schedulers, cpu, cpu_scheduler),
                                success(code)
                                {
                                    cs_return(Success(SchedulerInitStatus, SCHEDULER_INIT_OK));
                                },
                                error(code, message)
                                {
                                    cs_return(Error(SchedulerInitStatus, message, SCHEDULER_INIT_ERROR));
                                }
                            );
                        },
                        error(code, message)
                        {
                            cs_return(Error(SchedulerInitStatus, message, SCHEDULER_INIT_ERROR));
                        }
                    );
                },
                none()
                {
                    cs_return(Error(SchedulerInitStatus, bstr("Scheduler has no kernel"), SCHEDULER_INIT_ERROR));
                }
            );
        }
    );

    return Error(SchedulerInitStatus, unknown_error, SCHEDULER_INIT_ERROR);
}

void Scheduler__mark_ready(SchedulerPtr self)
{
    mutex_lock(&self->scheduler_lock);
    self->ready = true;
    mutex_unlock(&self->scheduler_lock);
}

bool Scheduler__ready(SchedulerPtr self)
{
    mutex_lock(&self->scheduler_lock);
    bool ready = self->ready;
    mutex_unlock(&self->scheduler_lock);

    return ready;
}

static volatile bool Scheduler__not_ready(void * data)
{
    SchedulerPtr self = data;
    return !Scheduler__ready(self);
}

Result(SchedulerInitStatus) Scheduler__wait_until_ready(SchedulerPtr self)
{
    Spin__wait_context(Scheduler__not_ready, self, MAX_SPINS);

    if (Scheduler__ready(self))
    {
        return Success(SchedulerInitStatus, SCHEDULER_INIT_OK);
    }

    return Error(SchedulerInitStatus, process_not_found, SCHEDULER_NOT_STARTED);
}

static Optional(ProcessPtr) Scheduler__current_process_locked(SchedulerPtr self, CPUID cpu)
{
    match_optional(
        CPUSchedulers__get(&self->schedulers, cpu),
        some(scheduler)
        {
            return CPUScheduler__current_process(scheduler);
        },
        none_do_nothing()
    );
    return None(ProcessPtr);
}

Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Scheduler__current_process_locked(self, cpu));
        }
    );

    return None(ProcessPtr);
}

Optional(ThreadPtr) Scheduler__current_thread(SchedulerPtr self, CPUID cpu)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    cs_return(CPUScheduler__current_thread(scheduler));
                },
                none_do_nothing()
            );
        }
    );

    return None(ThreadPtr);
}

bool Scheduler__should_preempt_on_timer(SchedulerPtr self, CPUID cpu)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    uint64_t now_usec = CPU__time_usec();
                    if (scheduler->started && now_usec - scheduler->last_registry_sample_usec >=
                                                  PROCESS_CPU_SHORT_TICKS * PROCESS_CPU_TICK_USEC)
                    {
                        CPUScheduler__publish_processes(scheduler);
                    }
                },
                none_do_nothing()
            );
            match_optional(
                Scheduler__current_process_locked(self, cpu),
                some(process)
                {
                    cs_return(Process__has_pending_signal(process) ||
                              !Process__is_handling_service_irq(process));
                },
                none_do_nothing()
            );
        }
    );

    return true;
}

Optional(ThreadPtr) Scheduler__save_context(SchedulerPtr self, CPUID cpu, const OptionalPtr(ThreadContextPtr) context)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    match_optional(
                        CPUScheduler__current_thread(scheduler),
                        some(thread)
                        {
                            ProcessPtr proc = thread->owner;
                            __d_printf(SCHEDULER, "Scheduler__save_context %s\n", cstr(proc->name));
                            match_result(
                                Thread__save_context(thread, context),
                                success(code)
                                {
                                    cs_return(Some(ThreadPtr, thread));
                                },
                                error(code, message)
                                {
                                    __d_printf(SCHEDULER,
                                               "Context save failed pid=%i name=%s code=%i message=%s\n",
                                               proc->pid, cstr(proc->name), code, cstr(message));
                                    cs_return(None(ThreadPtr));
                                }
                            );
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );

    return None(ThreadPtr);
}

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    cs_return(Some(ProcessPtr, scheduler->idle_process));
                },
                none_do_nothing()
            );
        }
    );

    return None(ProcessPtr);
}

SchedulerPtr Scheduler__heap_new(void)
{
    SchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(Scheduler)), Scheduler);
    *sched = NEW_SCHEDULER;
    return sched;
}

Result(SchedulerScheduleStatus) Scheduler__schedule(SchedulerPtr self, ProcessPtr new_proc)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                Scheduler__register_live_child_locked(self, new_proc),
                success_do_nothing(),
                error(code, message)
                {
                    cs_return(Error(SchedulerScheduleStatus, message, SCHEDULER_GENERAL_ERROR));
                }
            );
            match_result(
                ProcessTable__push(&self->admission, new_proc),
                success(index)
                {
                    new_proc->thread.state = PROC_STATE_READY;
                    cs_return(Success(SchedulerScheduleStatus, SCHEDULER_SCHEDULE_OK));
                },
                error(code, message)
                {
                    Scheduler__unlink_live_child_locked(new_proc);
                    cs_return(Error(SchedulerScheduleStatus, message, code));
                }
            );
        }
    );

    return Error(SchedulerScheduleStatus, unknown_error, SCHEDULER_GENERAL_ERROR);
}

static Result(ThreadID) Scheduler__publish_thread(SchedulerPtr self, ThreadPtr caller, ThreadPtr created)
{
    ProcessPtr process = caller->owner;
    if (process->lifecycle.terminating || process->next_thread_id > (ThreadID)INT64_MAX ||
        ProcessThreads__len(&process->threads) >= MAX_PROCESSES)
    {
        return Error(ThreadID, bstr("Process cannot create another thread"), THREAD_OPERATION_BUSY);
    }

    match_optional(
        CPUSchedulers__get(&self->schedulers, CPU__id()),
        some(cpu)
        {
            if (is_nothing(ThreadTable__find_item(&cpu->threads, caller)) ||
                is_nothing(ProcessTable__find_item(&cpu->processes, process)))
            {
                return Error(ThreadID, process_not_found, THREAD_OPERATION_NOT_FOUND);
            }
            match_result(
                ProcessThreads__push(&process->threads, created),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ThreadID, message, THREAD_OPERATION_NO_MEMORY);
                }
            );
            match_result(
                ThreadTable__push(&cpu->threads, created),
                success_do_nothing(),
                error(code, message)
                {
                    ProcessThreads__remove(&process->threads, created);
                    return Error(ThreadID, message, THREAD_OPERATION_NO_MEMORY);
                }
            );
            created->id = process->next_thread_id++;
            created->state = PROC_STATE_READY;
            return Success(ThreadID, created->id);
        },
        none_do_nothing()
    );
    return Error(ThreadID, process_not_found, THREAD_OPERATION_NOT_FOUND);
}

Result(ThreadID) Scheduler__create_thread(SchedulerPtr self, ThreadPtr caller, Address trampoline,
                                          Address start_function, Anything argument, Address thread_pointer)
{
    if (trampoline == 0 || start_function == 0)
    {
        return Error(ThreadID, bstr("Invalid thread entry"), THREAD_OPERATION_INVALID);
    }
    ProcessPtr process = caller->owner;
    size_t stack_size = SystemInfo__default_user_stack_size();
    match_optional(
        process->kernel->system_info,
        some(system_info)
        {
            stack_size = SystemInfo__user_stack_size(&system_info);
        },
        none_do_nothing()
    );
    match_result(
        Process__alloc_user_pages(process, stack_size),
        success(base)
        {
            Stack stack = Stack__new_from_user_allocation(base, stack_size);
            ThreadPtr created = v_ptr(Kernel__alloc(sizeof(Thread)), Thread);
            *created = Thread__new(process, &stack);
            created->owns_storage = true;
            created->context = Some(
                ThreadContextPtr,
                ThreadContext__init_thread(process->kernel, &stack,
                                           UserAddress__new(trampoline, NULL_KERNEL_ADDRESS, true, false, true, true),
                                           start_function, argument));
            ThreadContext__set_thread_pointer(Thread__required_context(created), thread_pointer);
            Result(ThreadID) published;
            critical_section(
                self->scheduler_lock,
                {
                    published = Scheduler__publish_thread(self, caller, created);
                }
            );
            match_result(
                published,
                success(id)
                {
                    return Success(ThreadID, id);
                },
                error(code, message)
                {
                    Thread__dispose_exit_resources(created);
                    Kernel__free(addr(created));
                    Process__free_user_pages(process, base.virtual_addr);
                    return Error(ThreadID, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(ThreadID, message, THREAD_OPERATION_NO_MEMORY);
        }
    );
    return Error(ThreadID, bstr("Thread creation failed"), THREAD_OPERATION_NO_MEMORY);
}

static bool Scheduler__thread_id_matches(ThreadPtr * candidate, void * context)
{
    return (*candidate)->id == *(ThreadID *)context;
}

static Result(ThreadJoinStatus)
    Scheduler__write_join_result(ProcessPtr process, ThreadJoinResultPtr result, Anything value)
{
    match_optional(
        Process__writable_user_kernel_mapping(process, addr(result), sizeof(ThreadJoinResult)),
        some(mapping)
        {
            ThreadJoinResult joined = ThreadJoinResult__new(value);
            Kernel__memcopy(addr(&joined), mapping.virtual_addr, sizeof(joined));
            return Success(ThreadJoinStatus, THREAD_OPERATION_OK);
        },
        none_do_nothing()
    );
    return Error(ThreadJoinStatus, bstr("Invalid thread join result"), THREAD_OPERATION_INVALID);
}

static Result(ThreadJoinStatus) Scheduler__reap_thread(SchedulerPtr self, ThreadPtr thread)
{
    if (thread == &thread->owner->thread)
    {
        return Success(ThreadJoinStatus, THREAD_OPERATION_OK);
    }

    ProcessPtr process = thread->owner;
    critical_section(
        self->scheduler_lock,
        {
            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                               scheduler,
                {
                    match_optional(
                        scheduler->switching_out,
                        some(outgoing)
                        {
                            if (outgoing == thread)
                            {
                                scheduler->switching_out = None(ThreadPtr);
                            }
                        },
                        none_do_nothing()
                    );
                }
            );
        }
    );
    match_result(
        Process__free_user_pages(process, thread->stack.base.virtual_addr),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ThreadJoinStatus, message, code);
        }
    );
    match_result(
        Thread__dispose_exit_resources(thread),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ThreadJoinStatus, message, code);
        }
    );
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                ProcessThreads__remove(&process->threads, thread),
                success_do_nothing(),
                error(code, message)
                {
                    cs_return(Error(ThreadJoinStatus, message, THREAD_OPERATION_NOT_FOUND));
                }
            );
        }
    );
    match_result(
        Kernel__free(addr(thread)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ThreadJoinStatus, message, code);
        }
    );
    return Success(ThreadJoinStatus, THREAD_OPERATION_OK);
}

static Result(Anything) Scheduler__complete_thread_join(ParkedOperationPtr operation)
{
    match_optional(
        operation->kernel_context->thread,
        some(joiner)
        {
            ProcessPtr process = joiner->owner;
            ThreadID target_id = unwrap_optional(operation->operation_key, 0);
            match_find_result(
                ProcessThreads__find(&process->threads, Scheduler__thread_id_matches, &target_id),
                some(target)
                {
                    target->joiner = None(ThreadPtr);
                    if (target->state != PROC_STATE_DEAD)
                    {
                        return Error(Anything, bstr("Thread join interrupted"), SYSCALL_INTERRUPTED);
                    }
                    match_result(
                        Scheduler__write_join_result(process, (ThreadJoinResultPtr)operation->operation_context,
                                                     target->exit_value),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(Anything, message, code);
                        }
                    );
                    target->joined = true;
                    match_optional(
                        operation->kernel_context->scheduler,
                        some(scheduler)
                        {
                            match_result(
                                Scheduler__reap_thread(scheduler, target),
                                success_do_nothing(),
                                error(code, message)
                                {
                                    return Error(Anything, message, code);
                                }
                            );
                        },
                        none_do_nothing()
                    );
                    return Success(Anything, THREAD_OPERATION_OK);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(Anything, bstr("Thread join target not found"), THREAD_OPERATION_NOT_FOUND);
}

Result(ThreadJoinStatus)
    Scheduler__join_thread(SchedulerPtr self, ThreadPtr caller, ThreadID target_id, ThreadJoinResultPtr result)
{
    if (target_id == caller->id || target_id == 0)
    {
        return Error(ThreadJoinStatus, bstr("Invalid thread join"), THREAD_OPERATION_INVALID);
    }
    ProcessPtr process = caller->owner;
    if (is_nothing(Process__writable_user_kernel_mapping(process, addr(result), sizeof(ThreadJoinResult))))
    {
        return Error(ThreadJoinStatus, bstr("Invalid thread join result"), THREAD_OPERATION_INVALID);
    }
    Optional(ThreadPtr) reap = None(ThreadPtr);
    Result(ThreadJoinStatus) outcome = Error(ThreadJoinStatus, bstr("Thread not found"), THREAD_OPERATION_NOT_FOUND);
    critical_section(
        self->scheduler_lock,
        {
            match_find_result(
                ProcessThreads__find(&process->threads, Scheduler__thread_id_matches, &target_id),
                some(target)
                {
                    if (target->joined || is_something(target->joiner))
                    {
                        cs_return(Error(ThreadJoinStatus, bstr("Thread already has a joiner"), THREAD_OPERATION_BUSY));
                    }
                    if (target->state == PROC_STATE_DEAD)
                    {
                        outcome = Scheduler__write_join_result(process, result, target->exit_value);
                        match_result(
                            outcome,
                            success(_)
                            {
                                target->joined = true;
                                reap = Some(ThreadPtr, target);
                            },
                            error(code, message)
                            {
                                outcome = Error(ThreadJoinStatus, message, code);
                            }
                        );
                    }
                    else
                    {
                        KernelContextPtr context =
                            KernelContext__heap_new(&Some(ThreadPtr, caller), &Some(SchedulerPtr, self));
                        match_optional(
                            CPUSchedulers__get(&self->schedulers, CPU__id()),
                            some(cpu)
                            {
                                match_result(
                                    CPUScheduler__park(
                                        cpu, process, context, &Some(Continuation, Scheduler__complete_thread_join),
                                        PARK_WAIT_THREAD, true, &Some(OperationKey, target_id), addr(result)),
                                    success(_)
                                    {
                                        target->joiner = Some(ThreadPtr, caller);
                                        cs_return(
                                            Error(ThreadJoinStatus, bstr("Thread join pending"), SYSCALL_IO_BLOCK));
                                    },
                                    error(code, message)
                                    {
                                        outcome = Error(ThreadJoinStatus, message, code);
                                    }
                                );
                            },
                            none_do_nothing()
                        );
                        KernelContext__dispose_heap(context);
                    }
                },
                none_do_nothing()
            );
        }
    );
    match_optional(
        reap,
        some(target)
        {
            match_result(
                Scheduler__reap_thread(self, target),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(ThreadJoinStatus, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return outcome;
}

Result(ThreadJoinStatus) Scheduler__exit_thread(SchedulerPtr self, ThreadPtr caller, Anything value)
{
    ProcessPtr process = caller->owner;
    bool other_live = false;
    critical_section(
        self->scheduler_lock,
        {
            for_each_ll(
                ProcessThreads,
                process->threads,
                thread,
                {
                    match_optional(
                        thread->joiner,
                        some(joiner)
                        {
                            if (joiner == caller)
                            {
                                thread->joiner = None(ThreadPtr);
                            }
                        },
                        none_do_nothing()
                    );
                    if (thread != caller && thread->state != PROC_STATE_DEAD)
                    {
                        other_live = true;
                    }
                }
            );
            if (other_live)
            {
                match_optional(
                    CPUSchedulers__get(&self->schedulers, CPU__id()),
                    some(cpu)
                    {
                        match_result(
                            ThreadTable__remove(&cpu->threads, caller),
                            success_do_nothing(),
                            error(code, message)
                            {
                                cs_return(Error(ThreadJoinStatus, message, THREAD_OPERATION_NOT_FOUND));
                            }
                        );
                        caller->exit_value = value;
                        caller->state = PROC_STATE_DEAD;
                        match_optional(
                            caller->joiner,
                            some(joiner)
                            {
                                if (Thread__mark_parked_operation_ready(joiner, PARK_WAIT_THREAD,
                                                                        &Some(OperationKey, caller->id),
                                                                        &Some(ReturnCode, THREAD_OPERATION_OK)))
                                {
                                    joiner->state = PROC_STATE_INTERRUPTED;
                                }
                            },
                            none_do_nothing()
                        );
                    },
                    none()
                    {
                        cs_return(Error(ThreadJoinStatus, process_not_found, THREAD_OPERATION_NOT_FOUND));
                    }
                );
            }
        }
    );

    if (!other_live)
    {
        match_result(
            Scheduler__exit(self, process, 0),
            success(_)
            {
                return Success(ThreadJoinStatus, THREAD_OPERATION_OK);
            },
            error(code, message)
            {
                return Error(ThreadJoinStatus, message, code);
            }
        );
    }
    Scheduler__yield_waiting(self, caller, CPU__id());
    return Success(ThreadJoinStatus, THREAD_OPERATION_OK);
}

Result(CPUID) Scheduler__schedule_on_cpu(SchedulerPtr self, ProcessPtr new_proc, CPUID cpu)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    match_result(
                        Scheduler__register_live_child_locked(self, new_proc),
                        success_do_nothing(),
                        error(code, message)
                        {
                            cs_return(Error(CPUID, message, SCHEDULER_GENERAL_ERROR));
                        }
                    );
                    Result(CPUID) result = CPUScheduler__schedule(scheduler, new_proc);
                    match_result(
                        result,
                        success(scheduled_cpu)
                        {
                            cs_return(Success(CPUID, scheduled_cpu));
                        },
                        error(code, message)
                        {
                            Scheduler__unlink_live_child_locked(new_proc);
                            cs_return(Error(CPUID, message, code));
                        }
                    );
                },
                none()
                {
                    BString invalid_cpu = BString__sprintf(&bstr("Invalid CPU %i\n"), cpu);

                    cs_return(Error(CPUID, invalid_cpu, SCHEDULER_INVALID_CPU));
                }
            );
        }
    );

    return Error(CPUID, unknown_error, SCHEDULER_GENERAL_ERROR);
}

Result(SchedulerScheduleStatus) Scheduler__promote_realtime(SchedulerPtr self, ProcessPtr process)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                Scheduler__scheduler_owning_process_locked(self, process),
                some(owner)
                {
                    if (!owner->started)
                    {
                        cs_return(
                            Error(SchedulerScheduleStatus, bstr("Process CPU is not started"), SCHEDULER_NOT_STARTED));
                    }
                    process->scheduling.process_class = PROCESS_CLASS_REALTIME_SERVICE;
                    Scheduler__forget_preferred(self, process);
                    cs_return(Success(SchedulerScheduleStatus, SCHEDULER_SCHEDULE_OK));
                },
                none()
                {
                    cs_return(Error(SchedulerScheduleStatus, process_not_found, SCHEDULER_PROCESS_NOT_FOUND));
                }
            );
        }
    );

    return Error(SchedulerScheduleStatus, unknown_error, SCHEDULER_GENERAL_ERROR);
}

Result(CPUID) Scheduler__cpu_for_process(SchedulerPtr self, ProcessPtr process)
{
    critical_section(
        self->scheduler_lock,
        {
            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                sched,
                {
                    match_optional(
                        ProcessTable__find_item(&sched->processes, process),
                        some(index)
                        {
                            cs_return(Success(CPUID, cpu));
                        },
                        none_do_nothing()
                    );
                }
            );
        }
    );

    return Error(CPUID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ServiceCode) Scheduler__subscribe_irq(SchedulerPtr self, ProcessPtr process, IRQNumber irq)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(
                ServiceEventDispatcher__subscribe_irq(&self->service_event_dispatcher, process, irq));
        }
    );

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__send_irq_event(SchedulerPtr self, IRQNumber irq)
{
    __d_printf(IRQ, "scheduler send irq event irq=%i\n", irq);
    match_optional(
        self->interrupt_controller,
        some(interrupt_controller)
        {
            critical_section(
                self->scheduler_lock,
                {
                    match_result(
                        ServiceEventDispatcher__dispatch_irq(&self->service_event_dispatcher, &interrupt_controller,
                                                             irq),
                        success(dispatch)
                        {
                            match_optional(
                                dispatch.process,
                                some(process)
                                {
                                    match_result(
                                        Scheduler__service_mark_runnable_locked(self, process),
                                        success_do_nothing(),
                                        error(code, message)
                                        {
                                            cs_return(Error(ServiceCode, message, code));
                                        }
                                    );
                                },
                                none_do_nothing()
                            );
                        },
                        error(code, message)
                        {
                            cs_return(Error(ServiceCode, message, code));
                        }
                    );
                }
            );
            return Success(ServiceCode, SERVICE_OK);
        },
        none()
        {
            return Error(ServiceCode, missing_interrupt_controller, SERVICE_SCHEDULER_ERROR);
        }
    );
}

Result(ServiceEvent) Scheduler__service_take_next_event(SchedulerPtr self, ProcessPtr process)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(ServiceEventDispatcher__take_next(&self->service_event_dispatcher, process));
        }
    );

    return Error(ServiceEvent, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ProcessChildEvent)
    Scheduler__child_take_event(SchedulerPtr self, ProcessPtr process, int64_t child_selection, uint64_t options)
{
    critical_section(
        self->scheduler_lock,
        {
            cs_return(Process__pop_child_event(process, child_selection, options));
        }
    );

    return Error(ProcessChildEvent, child_event_queue_empty, PROCESS_CHILD_EVENT_QUEUE_EMPTY);
}

Result(ServiceEvent) Scheduler__service_wait(SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context,
                                             OptionalPtr(Continuation) continuation)
{
    return Scheduler__service_wait_with_context_ownership(self, process, kernel_context, continuation, false);
}

Result(ServiceEvent)
    Scheduler__service_wait_with_context_ownership(SchedulerPtr self, ProcessPtr process,
                                                   KernelContextPtr kernel_context,
                                                   OptionalPtr(Continuation) continuation, bool owns_kernel_context)
{
    match_optional(
        self->interrupt_controller,
        some(interrupt_controller)
        {
            critical_section(
                self->scheduler_lock,
                {
                    match_result(
                        ServiceEventDispatcher__complete_active_irq(&self->service_event_dispatcher,
                                                                    &interrupt_controller, process),
                        success_do_nothing(),
                        error(code, message)
                        {
                            cs_return(Error(ServiceEvent, message, code));
                        }
                    );

                    match_result(
                        ServiceEventDispatcher__take_next(&self->service_event_dispatcher, process),
                        success(event)
                        {
                            cs_return(Success(ServiceEvent, event));
                        },
                        error(code, message)
                        {
                            if (code != SERVICE_EVENT_QUEUE_EMPTY)
                            {
                                cs_return(Error(ServiceEvent, message, code));
                            }
                        }
                    );

                    for_each_hashtable(
                        CPUSchedulers,
                        self->schedulers,
                        bucket_index,
                        entry_index,
                        cpu,
                        sched,
                        {
                            match_result(
                                CPUScheduler__park(sched, process, kernel_context, continuation,
                                                   PARK_WAIT_SERVICE_EVENT, owns_kernel_context,
                                                   &None(OperationKey), 0),
                                success(parked)
                                {
                                    cs_return(Error(ServiceEvent, service_event_queue_empty,
                                                    SERVICE_EVENT_QUEUE_EMPTY));
                                },
                                error(code, message)
                                {
                                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                                    {
                                        cs_return(Error(ServiceEvent, message, code));
                                    }
                                }
                            );
                        }
                    );
                }
            );

            BString no_scheduler = BString__sprintf(&bstr("No scheduler found for process: %i\n"), process->pid);
            return Error(ServiceEvent, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
        },
        none()
        {
            return Error(ServiceEvent, missing_interrupt_controller, SERVICE_SCHEDULER_ERROR);
        }
    );
}

typedef struct ChildSelection
{
    ProcessPtr parent;
    int64_t child_selection;
} ChildSelection;

typedef ChildSelection * ChildSelectionPtr;

static ChildSelection ChildSelection__new(ProcessPtr parent, int64_t child_selection)
{
    return (ChildSelection){
        .parent = parent,
        .child_selection = child_selection,
    };
}

static bool Scheduler__matches_child(ProcessPtr * candidate, void * context)
{
    ChildSelectionPtr selection = context;
    int64_t child_selection = selection->child_selection;
    bool selected = (child_selection > 0 && (*candidate)->pid == (ProcessID)child_selection) || child_selection == -1 ||
                    (child_selection == 0 && (*candidate)->process_group_id == selection->parent->process_group_id) ||
                    (child_selection < -1 && (*candidate)->process_group_id == (ProcessGroupID)-child_selection);
    if (!selected)
    {
        return false;
    }

    match_optional((*candidate)->parent,
        some(parent)
        {
            return parent == selection->parent;
        },
        none_do_nothing()
    );

    return false;
}

static bool Scheduler__has_live_child_locked(ProcessPtr process, int64_t child_selection)
{
    ChildSelection selection = ChildSelection__new(process, child_selection);
    match_find_result(
        LiveChildren__find(&process->live_children, Scheduler__matches_child, &selection),
        some(child)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

Result(ProcessChildEvent)
    Scheduler__child_wait_with_context_ownership(SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context,
                                                 OptionalPtr(Continuation) continuation, bool owns_kernel_context,
                                                 int64_t child_selection, uint64_t options)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                Process__pop_child_event(process, child_selection, options),
                success(event)
                {
                    cs_return(Success(ProcessChildEvent, event));
                },
                error(code, message)
                {
                    if (code != PROCESS_CHILD_EVENT_QUEUE_EMPTY)
                    {
                        cs_return(Error(ProcessChildEvent, message, code));
                    }
                }
            );

            if (!Scheduler__has_live_child_locked(process, child_selection))
            {
                cs_return(Error(ProcessChildEvent, bstr("No matching child process"), PROCESS_CHILD_EVENT_NO_CHILD));
            }

            if ((options & PROCESS_CHILD_WAIT_NOHANG) != 0)
            {
                cs_return(Error(ProcessChildEvent, child_event_queue_empty, PROCESS_CHILD_EVENT_PENDING));
            }

            for_each_hashtable(
                CPUSchedulers,
                self->schedulers,
                bucket_index,
                entry_index,
                cpu,
                sched,
                {
                    match_result(
                        CPUScheduler__park(sched, process, kernel_context, continuation,
                                           PARK_WAIT_CHILD_EVENT, owns_kernel_context,
                                           &Some(OperationKey, (OperationKey)child_selection), options),
                        success(parked)
                        {
                            cs_return(Error(ProcessChildEvent, child_event_queue_empty,
                                            PROCESS_CHILD_EVENT_QUEUE_EMPTY));
                        },
                        error(code, message)
                        {
                            if (code != SCHEDULER_PROCESS_NOT_FOUND)
                            {
                                cs_return(Error(ProcessChildEvent, message, code));
                            }
                        }
                    );
                }
            );
        }
    );

    BString no_scheduler = BString__sprintf(&bstr("No scheduler found for process: %i\n"), process->pid);
    return Error(ProcessChildEvent, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(ServiceCode) Scheduler__service_mark_runnable_locked(SchedulerPtr self, ProcessPtr process)
{
    Process__wake_shared_wait(process);
    Scheduler__wake_kernel_queue_waiters_locked(self, process);
    if (Scheduler__is_parked_for(self, process, PARK_WAIT_SERVICE_EVENT))
    {
        __d_printf(SCHEDULER, "service enqueue unparking pid=%i\n", process->pid);
        for_each_hashtable(
            CPUSchedulers,
            self->schedulers,
            bucket_index,
            entry_index,
            cpu,
            sched,
            {
                Optional(ReturnCode) return_code = None(ReturnCode);
                match_result(
                    CPUScheduler__unpark(sched, process, PARK_WAIT_SERVICE_EVENT, &None(ReturnCode)),
                    success(unparked)
                    {
                        __d_printf(SCHEDULER, "service enqueue unparked pid=%i cpu=%i state=%s\n",
                                   process->pid, cpu, ProcessState__to_string(process->thread.state));
                        return Success(ServiceCode, SERVICE_OK);
                    },
                    error(code, message)
                    {
                        if (code != SCHEDULER_PROCESS_NOT_FOUND)
                        {
                            return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR);
                        }
                    }
                );
            }
        );
    }

    return Success(ServiceCode, SERVICE_OK);

    BString no_scheduler = BString__sprintf(&bstr("No scheduler found for process: %i\n"), process->pid);
    return Error(ServiceCode, no_scheduler, SERVICE_SCHEDULER_ERROR);
}

Result(ServiceCode) Scheduler__service_enqueue(SchedulerPtr self, ProcessPtr process, ConstServiceEventPtr event)
{
    critical_section(
        self->scheduler_lock,
        {
            match_result(
                ServiceEventDispatcher__enqueue(&self->service_event_dispatcher, process, *event),
                success_do_nothing(),
                error(code, message)
                {
                    cs_return(Error(ServiceCode, message, code));
                }
            );

            cs_return(Scheduler__service_mark_runnable_locked(self, process));
        }
    );

    return Success(ServiceCode, SERVICE_OK);
}

static Result(ServiceCode) Scheduler__enqueue_memory_pressure_locked(SchedulerPtr self, ProcessPtr process,
                                                                     MemPressureLevel pressure, uint64_t free_pages)
{
    ServiceEvent event = ServiceEvent__new(SERVICE_EVENT_MEMORY_PRESSURE, pressure, free_pages);
    match_result(
        ServiceEventDispatcher__enqueue(&self->service_event_dispatcher, process, event),
        success_do_nothing(),
        error(code, message)
        {
            return Error(ServiceCode, message, code);
        }
    );
    return Scheduler__service_mark_runnable_locked(self, process);
}

Result(ServiceCode) Scheduler__register_memory_reclaimer(SchedulerPtr self, ProcessPtr process,
                                                         MemPressureLevel pressure, uint64_t free_pages)
{
    critical_section(
        self->scheduler_lock,
        {
            bool was_registered = process->service_state.memory_reclaimer;
            process->service_state.memory_reclaimer = true;
            if (!was_registered && pressure != MEM_PRESSURE_NORMAL)
            {
                cs_return(Scheduler__enqueue_memory_pressure_locked(self, process, pressure, free_pages));
            }
        }
    );

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__notify_memory_pressure(SchedulerPtr self, uint64_t generation, MemPressureLevel pressure,
                                                      uint64_t free_pages)
{
    critical_section(
        self->scheduler_lock,
        {
            if (generation == self->memory_pressure_generation)
            {
                cs_return(Success(ServiceCode, SERVICE_OK));
            }
            self->memory_pressure_generation = generation;
            ProcessTable processes = Scheduler__all_processes_for_all_cpus_locked(self);
            for_each_fa_ptr(
                ProcessTable,
                &processes,
                index,
                process,
                {
                    if (process->service_state.memory_reclaimer)
                    {
                        match_result(
                            Scheduler__enqueue_memory_pressure_locked(self, process, pressure, free_pages),
                            success_do_nothing(),
                            error(code, message)
                            {
                                __d_printf(SCHEDULER, "memory pressure notification failed pid=%i code=%i message=%s\n",
                                           process->pid, code, BString__cstr(&message));
                            }
                        );
                    }
                }
            );
        }
    );

    return Success(ServiceCode, SERVICE_OK);
}

static Result(ServiceCode)
    Scheduler__service_ready_mark_runnable_locked(SchedulerPtr self, ProcessPtr process, ServiceCode result)
{
    if (Scheduler__is_parked_for(self, process, PARK_WAIT_SERVICE_READY))
    {
        for_each_hashtable(
            CPUSchedulers,
            self->schedulers,
            bucket_index,
            entry_index,
            cpu,
            sched,
            {
                Optional(ReturnCode) return_code = Some(ReturnCode, result);
                match_result(
                    CPUScheduler__unpark(sched, process, PARK_WAIT_SERVICE_READY, &return_code),
                    success(unparked)
                    {
                        return Success(ServiceCode, SERVICE_OK);
                    },
                    error(code, message)
                    {
                        if (code != SCHEDULER_PROCESS_NOT_FOUND)
                        {
                            return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR);
                        }
                    }
                );
            }
        );
    }

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__service_publish_ready(SchedulerPtr self, ProcessPtr process, ConstBStringPtr name)
{
    critical_section(
        self->scheduler_lock,
        {
            ServiceReadyProcesses ready_processes = ServiceReadyProcesses__new();
            match_result(
                ServiceEventDispatcher__publish_ready(&self->service_event_dispatcher, process, name, &ready_processes),
                success_do_nothing(),
                error(code, message)
                {
                    cs_return(Error(ServiceCode, message, code));
                }
            );

            for_each_fa(
                ServiceReadyProcesses,
                ready_processes,
                index,
                process,
                {
                    match_result(
                        Scheduler__service_ready_mark_runnable_locked(self, process, SERVICE_OK),
                        success_do_nothing(),
                        error(code, message)
                        {
                            cs_return(Error(ServiceCode, message, code));
                        }
                    );
                }
            );
        }
    );

    return Success(ServiceCode, SERVICE_OK);
}

Optional(ProcessPtr) Scheduler__service_ready_process(SchedulerPtr self, ConstBStringPtr name)
{
    return ServiceEventDispatcher__ready_process(&self->service_event_dispatcher, name);
}

Result(ServiceCode)
    Scheduler__service_wait_ready_with_context_ownership(SchedulerPtr self, ProcessPtr process, ConstBStringPtr name,
                                                         KernelContextPtr kernel_context,
                                                         OptionalPtr(Continuation) continuation,
                                                         bool owns_kernel_context)
{
    if (!ProcessClass__is_service(process->scheduling.process_class))
    {
        return Error(ServiceCode, service_event_queue_empty, SERVICE_NOT_SERVICE_PROCESS);
    }

    critical_section(
        self->scheduler_lock,
        {
            match_result(
                ServiceEventDispatcher__wait_ready(&self->service_event_dispatcher, process, name),
                success(code)
                {
                    if (code == SERVICE_OK && ServiceEventDispatcher__is_ready(&self->service_event_dispatcher, name))
                    {
                        cs_return(Success(ServiceCode, SERVICE_OK));
                    }

                    for_each_hashtable(
                        CPUSchedulers,
                        self->schedulers,
                        bucket_index,
                        entry_index,
                        cpu,
                        sched,
                        {
                            match_result(
                                CPUScheduler__park(sched, process, kernel_context, continuation,
                                                   PARK_WAIT_SERVICE_READY, owns_kernel_context, &None(OperationKey),
                                                   0),
                                success(parked)
                                {
                                    cs_return(Error(ServiceCode, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY));
                                },
                                error(code, message)
                                {
                                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                                    {
                                        cs_return(Error(ServiceCode, message, code));
                                    }
                                }
                            );
                        }
                    );
                },
                error(code, message)
                {
                    cs_return(Error(ServiceCode, message, code));
                }
            );
        }
    );

    BString no_scheduler = BString__sprintf(&bstr("No scheduler found for process: %i\n"), process->pid);
    return Error(ServiceCode, no_scheduler, SERVICE_SCHEDULER_ERROR);
}

static Result(ServiceCode)
    Scheduler__child_mark_runnable_locked(SchedulerPtr self, ProcessPtr process, ProcessID child_pid,
                                          ProcessGroupID child_process_group_id, uint64_t event_type)
{
    Process__wake_shared_wait(process);
    bool wait_ready = false;
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu,
        scheduler,
        {
            for_each_fa(
                ThreadTable,
                scheduler->threads,
                thread_index,
                thread,
                {
                    if (!wait_ready && thread->owner == process &&
                        Thread__mark_child_wait_ready(thread, child_pid, child_process_group_id, event_type))
                    {
                        thread->state = PROC_STATE_INTERRUPTED;
                        wait_ready = true;
                    }
                }
            );
        }
    );

    Scheduler__trace_child_wake(process, child_pid, event_type, wait_ready);

    return Success(ServiceCode, SERVICE_OK);
}

Optional(ContextSwitch) Scheduler__interrupt(SchedulerPtr self, CPUID cpu, const OptionalPtr(ThreadContextPtr) context)
{
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    if (!scheduler->started)
                    {
                        cs_return(None(ContextSwitch));
                    }
                    __d_printf(SCHEDULER, "CPUScheduler__interrupt %i\n", cpu);
                    match_optional(
                        scheduler->switching_out,
                        some(outgoing)
                        {
                            outgoing->migration_safe = true;
                        },
                        none_do_nothing()
                    );
                    scheduler->switching_out = None(ThreadPtr);
                    Scheduler__poll_queue_waiters(self);
                    Scheduler__poll_exec_load_waiters(scheduler);
                    Scheduler__refresh_preferred(self);
                    Optional(ContextSwitch) next = CPUScheduler__interrupt(scheduler, context);
                    match_optional(
                        next,
                        some(selected)
                        {
                            scheduler->dispatching_process = Some(ProcessPtr, selected.new_thread->owner);
                        },
                        none_do_nothing()
                    );
                    cs_return(next);
                },
                none()
                {
                    __d_printf(PANIC, "Invalid CPU %i\n", cpu);
                    cs_return(None(ContextSwitch));
                }
            );
        }
    );

    return None(ContextSwitch);
}

void Scheduler__preempt(SchedulerPtr self, CPUID cpu, const OptionalPtr(ThreadContextPtr) context)
{
    Optional(ContextSwitch) context_switch = Scheduler__interrupt(self, cpu, context);
    match_optional(context_switch,
        some(next)
        {
            ContextSwitch__switch(&next);
        },
        none_do_nothing()
    );
}

void Scheduler__yield_runnable(SchedulerPtr self, ThreadPtr yielding_thread, CPUID cpu)
{
    Optional(ContextSwitch) context_switch = None(ContextSwitch);
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    Scheduler__poll_queue_waiters(self);
                    Scheduler__refresh_preferred(self);
                    Scheduler__forget_preferred(self, yielding_thread->owner);
                    context_switch = CPUScheduler__yield_runnable(scheduler, yielding_thread);
                    match_optional(
                        context_switch,
                        some(selected)
                        {
                            scheduler->dispatching_process = Some(ProcessPtr, selected.new_thread->owner);
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );

    match_optional(context_switch,
        some(next)
        {
            ContextSwitch__switch(&next);
        },
        none_do_nothing()
    );
}

void Scheduler__yield_waiting(SchedulerPtr self, ThreadPtr waiting_thread, CPUID cpu)
{
    ProcessPtr waiting_proc = waiting_thread->owner;
    Optional(ContextSwitch) context_switch = None(ContextSwitch);
    __d_printf(SCHEDULER, "Scheduler__yield_waiting cpu=%i waiting pid=%i name=%s\n", cpu, waiting_proc->pid,
               cstr(waiting_proc->name));
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    Scheduler__poll_queue_waiters(self);
                    Scheduler__refresh_preferred(self);
                    context_switch = CPUScheduler__yield_waiting(scheduler, waiting_thread);
                    match_optional(
                        context_switch,
                        some(selected)
                        {
                            scheduler->dispatching_process = Some(ProcessPtr, selected.new_thread->owner);
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );

    match_optional(
        context_switch,
        some(next)
        {
            __d_printf(SCHEDULER, "Scheduler__yield_waiting switch cpu=%i from pid=%i to pid=%i name=%s\n", cpu,
                       waiting_proc->pid, next.new_thread->owner->pid, cstr(next.new_thread->owner->name));
            ContextSwitch__switch(&next);
        },
        none()
        {
            __d_printf(SCHEDULER, "Scheduler__yield_waiting no switch cpu=%i pid=%i\n", cpu, waiting_proc->pid);
        }
    );
}

Result(SchedulerInitStatus) Scheduler__start(SchedulerPtr self, CPUID cpu)
{
    __d_printf(SCHEDULER, "Scheduler__start cpu=%i\n", cpu);
    // we just spawn the idle process and new process starts will rely on the queues
    critical_section(
        self->scheduler_lock,
        {
            match_optional(
                CPUSchedulers__get(&self->schedulers, cpu),
                some(scheduler)
                {
                    __d_printf(SCHEDULER,
                               "Scheduler__start found cpu=%i scheduler=%p idle=%p started=%i\n", cpu,
                               scheduler, scheduler->idle_process, scheduler->started);
                                 // will never return from here
                    scheduler->started = true;
                    cs_exit_then(CPUScheduler__start(scheduler));
                },
                none()
                {
                    BString invalid_cpu = BString__sprintf(&bstr("Invalid CPU %i\n"), cpu);

                    cs_return(Error(SchedulerInitStatus, invalid_cpu, SCHEDULER_INVALID_CPU));
                    __d_println(MUTEX, "Unknown error");
                }
            );
        }
    );

    return Error(SchedulerInitStatus, BString__sprintf(&bstr("Scheduler failed to start CPU %i\n"), cpu),
                 SCHEDULER_INIT_ERROR);
}

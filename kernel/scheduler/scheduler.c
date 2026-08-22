#include "scheduler.h"
#include "../arch/process.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "../kalloc.h"
#include "../print.h"
#include "../all_defs.h"
#include "../kernel_core.h"
#include "../spin.h"
#include "parked.h"
#include "../subsystems/message/subsystem_codes.h"
#include "../subsystems/io/io_subsystem.h"
#include "../service/service.h"
#include "idle_debug.h"

FixedArrayBodyDefine(CPUSchedulers);

#define CPU_SCHEDULER_INIT(idle, cpu) \
    (CPUScheduler) { \
        .idle_process = idle, \
        .processes = EmptyArray(ProcessTable), \
        .scheduler_lock = MUTEX_INIT, \
        .last_tick = 0, \
        .cpu = cpu, \
        .started = false, \
    }

BString unreachable_code = BSTRING_NEW("Unreachable code reached\n");
BString process_not_found = BSTRING_NEW("Process not found\n");
BString reason_mismatch_format = BSTRING_NEW("Process was parked for reason %i, not %i\n");
BString process_wasnt_parked = BSTRING_NEW("Process wasn't parked\n");
static BString abort_format = BSTRING_NEW(
    "process fault: %s exception=%s fsc=%i:%s access=%s pc=0x%x far=0x%x esr=0x%x ec=0x%x cpu=%i\n"
);
static const BString data_abort_reason = BSTRING_NEW("data abort");
static const BString instruction_abort_reason = BSTRING_NEW("instruction abort");
static const BString undefined_instruction_reason = BSTRING_NEW("undefined instruction");
static const BString alignment_fault_reason = BSTRING_NEW("alignment fault");
static const BString bad_syscall_reason = BSTRING_NEW("bad syscall");
static const BString unknown_user_exception_reason = BSTRING_NEW("unknown user exception");
static const BString unknown_fault_reason = BSTRING_NEW("unknown fault");
static BString service_event_queue_empty = BSTRING_NEW("Service event queue is empty");
static BString child_event_queue_empty = BSTRING_NEW("Child event queue is empty");
static BString missing_interrupt_controller = BSTRING_NEW("Missing interrupt controller info");
static BString no_cpus_available = BSTRING_NEW("No CPUs available");
static BString process_user_root = BSTRING_NEW("root");
static BString process_user_hund = BSTRING_NEW("hund");
static BString process_user_user = BSTRING_NEW("user");
static BString process_user_unknown = BSTRING_NEW("unknown");

static Result(ServiceCode) Scheduler__service_mark_runnable_locked(SchedulerPtr self, ProcessPtr process);
static Result(ServiceCode) Scheduler__child_mark_runnable_locked(SchedulerPtr self, ProcessPtr process);
static Result(ServiceCode) Scheduler__service_ready_mark_runnable_locked(
    SchedulerPtr self, ProcessPtr process, ServiceCode result
);
static Result(ServiceCode) Scheduler__notify_parent_child_exit_locked(
    SchedulerPtr self, ProcessPtr parent, ProcessPtr child, ReturnCode return_code
);
static Result(ProcessPtr) Scheduler__park_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason,
    bool owns_kernel_context, Optional(OperationKey) operation_key
);
static Result(ProcessPtr) Scheduler__unpark_with_operation_key(
    SchedulerPtr self, ProcessPtr proc, ParkReason reason,
    Optional(ReturnCode) return_code, Optional(OperationKey) operation_key
);
static Result(ProcessPtr) CPUScheduler__unpark_with_operation_key(
    CPUSchedulerPtr self, ProcessPtr proc, ParkReason reason,
    Optional(ReturnCode) return_code, Optional(OperationKey) operation_key
);

static Result(AckCode) Scheduler__publish_process_state(SchedulerPtr self, ProcessPtr proc)
{
    MessagePtr message = Message__heap_new_process_id(SOURCE_PROCESS, proc->state, proc->pid);
    return MessageBroker__deliver(self->message_broker, message);
}

Result(CPUSchedulerPtr) CPUScheduler__heap_new(KernelPtr kernel, Elf idle_process, CPUID cpu)
{
    ResultMatch(ProcessPtr, Process__setup(kernel, idle_process, None(ProcessPtr)))
        IsSuccess(idle,
        {
            CPUSchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(CPUScheduler)), CPUScheduler);
            *sched = CPU_SCHEDULER_INIT(idle, cpu);
            ResultMatch(UserAddress, Process__map_idle_debug_page(idle))
                IsError(code, message, return Error(CPUSchedulerPtr, message, code))
            End
            return Success(CPUSchedulerPtr, sched);
        });
        IsError(code, message, return Error(CPUSchedulerPtr, message, code));
    End
}

bool process_in_state(ProcessPtr * proc, void * compare_state)
{
    ProcessState state = (*proc)->state;
    ProcessState compare = *(ProcessState *) compare_state;
    __d_printf(SCHEDULER, "process_in_state %s %s\n", cstr((*proc)->name), ProcessState__to_string(state));

    return state & compare;
}

Result(CPUID) CPUScheduler__schedule(CPUSchedulerPtr self, ProcessPtr new_proc)
{
    __d_printf(SCHEDULER, "Queuing %s %s\n", cstr(new_proc->name), ProcessState__to_string(new_proc->state));
    CriticalSection(self->scheduler_lock,
    {
        new_proc->state = PROC_STATE_READY;
        __d_printf(SCHEDULER, "%s %s\n", cstr(new_proc->name), ProcessState__to_string(new_proc->state));
        ProcessTable__push(&self->processes, new_proc);
        _cs_return Success(CPUID, self->cpu);
    });
}

int longest_waiting(ProcessPtr * proc1, ProcessPtr * proc2)
{
    if ((*proc2)->last_tick > (*proc1)->last_tick)
    {
        return 1;
    }
    if ((*proc2)->last_tick < (*proc1)->last_tick)
    {
        return -1;
    }
    return 0;
}

int service_priority_then_longest_waiting(ProcessPtr * proc1, ProcessPtr * proc2)
{
    uint64_t proc1_priority = ProcessClass__priority((*proc1)->process_class);
    uint64_t proc2_priority = ProcessClass__priority((*proc2)->process_class);

    if (proc1_priority > proc2_priority)
    {
        return 1;
    }
    if (proc1_priority < proc2_priority)
    {
        return -1;
    }

    return longest_waiting(proc1, proc2);
}

Optional(ProcessPtr) CPUScheduler__current_process(CPUSchedulerPtr self)
{
    ProcessState running_state = PROC_STATE_RUNNING;
    FindResultOptional(ProcessTable) current_match = ProcessTable__find(&self->processes, process_in_state, &running_state);

    FindResultMatch(ProcessTable, current_match)
        IsSome(match,
        {
            return Some(ProcessPtr, *match.value);
        })
        IsNone(
        {
            if (self->idle_process->state == PROC_STATE_RUNNING)
            {
                return Some(ProcessPtr, self->idle_process);
            }
            return None(ProcessPtr);
        })
    End

    return None(ProcessPtr);
}

bool CPUScheduler__is_idle(CPUSchedulerPtr self)
{
    ProcessState running_state = PROC_STATE_RUNNING;
    FindResultOptional(ProcessTable) current_match = ProcessTable__find(&self->processes, process_in_state, &running_state);

    FindResultMatch(ProcessTable, current_match)
        IsSome(match,
        {
            return false;
        })
        IsNone(
        {
            return true;
        })
    End

    return true;
}

uint64_t Scheduler__process_count(SchedulerPtr self)
{
    uint64_t count = 0;
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        count += ProcessTable__len(&sched->processes);
    End

    return count;
}

Result(ProcessPtr) CPUScheduler__exit(CPUSchedulerPtr self, ProcessPtr proc)
{
    __d_printf(SCHEDULER, "CPUScheduler__exit %s\n", cstr(proc->name));

    Result(CollectionCode) remove_res;

    CriticalSection(self->scheduler_lock, remove_res = ProcessTable__remove(&self->processes, proc);)

    ResultMatch(CollectionCode, remove_res)
        IsError(code, message,
        {
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        })
        IsSuccess(process_index,
        {
            proc->state = PROC_STATE_DEAD;
            return Success(ProcessPtr, proc);
        })
    End
}

Result(ProcessPtr) CPUScheduler__park(
    CPUSchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason,
    bool owns_kernel_context, Optional(OperationKey) operation_key
)
{
    __d_printf(SCHEDULER, "CPUScheduler__park %s\n", cstr(proc->name));
    Match(ArrayIndex, ProcessTable__find_item(&self->processes, proc))
        IsSome(index,
        {
            ParkedOperationPtr parked = ParkedOperation__heap_new_with_context_ownership(
                kernel_context, kernel_continuation, reason, owns_kernel_context
            );
            parked->operation_key = operation_key;
            proc->state = PROC_STATE_WAITING;
            __d_printf(SCHEDULER, "CPUScheduler__park - context %i\n", parked);
            ResultMatch(CollectionCode, Process__push_parked_operation(proc, parked))
                IsError(code, message, return Error(ProcessPtr, message, code))
            End
            return Success(ProcessPtr, proc);
        })
        IsNone(
        {
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        })
    End
}

Result(ProcessPtr) CPUScheduler__unpark(CPUSchedulerPtr self, ProcessPtr proc, ParkReason reason, Optional(ReturnCode) return_code)
{
    return CPUScheduler__unpark_with_operation_key(self, proc, reason, return_code, None(OperationKey));
}

static Result(ProcessPtr) CPUScheduler__unpark_with_operation_key(
    CPUSchedulerPtr self, ProcessPtr proc, ParkReason reason,
    Optional(ReturnCode) return_code, Optional(OperationKey) operation_key
)
{
    __d_printf(SCHEDULER, "CPUScheduler__unpark %s\n", cstr(proc->name));
    __d_printf(
        SCHEDULER,
        "CPUScheduler__unpark cpu=%i target pid=%i name=%s state=%s reason=%i\n",
        self->cpu,
        proc->pid,
        cstr(proc->name),
        ProcessState__to_string(proc->state),
        reason
    );
    Match(ArrayIndex, ProcessTable__find_item(&self->processes, proc))
        IsSome(index,
        {
            __d_printf(SCHEDULER, "CPUScheduler__unpark - done %s\n", cstr(proc->name));
            __d_printf(SCHEDULER, "CPUScheduler__unpark cpu=%i found target index=%i\n", self->cpu, index);
            if (Process__mark_parked_operation_ready(proc, reason, operation_key, return_code))
            {
                proc->state = PROC_STATE_INTERRUPTED;
                __d_printf(
                    SCHEDULER,
                    "CPUScheduler__unpark cpu=%i success pid=%i state=%s\n",
                    self->cpu,
                    proc->pid,
                    ProcessState__to_string(proc->state)
                );
                return Success(ProcessPtr, proc);
            }
            else
            {
                __d_printf(SCHEDULER, "CPUScheduler__unpark cpu=%i target pid=%i was not parked\n", self->cpu, proc->pid);
                return Error(ProcessPtr, process_wasnt_parked, SCHEDULER_CANT_UNPARK);
            }

        })
        IsNone(
        {
            __d_printf(SCHEDULER, "CPUScheduler__unpark cpu=%i target pid=%i not found\n", self->cpu, proc->pid);
            return Error(ProcessPtr, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
        })
    End
}

Optional(ContextSwitch) CPUScheduler__interrupt(CPUSchedulerPtr self, ProcessContextPtr context)
{
    ProcessState ready_state = PROC_STATE_READY | PROC_STATE_INTERRUPTED;

    Optional(ProcessPtr) current = CPUScheduler__current_process(self);
    CriticalSection(self->scheduler_lock, self->last_tick++;)

    Match(ProcessPtr, current)
        IsSome(proc,
        {
            __d_printf(SCHEDULER, "CPUScheduler__interrupt %s\n", cstr(proc->name));
            Process__save_context(proc, context);
            proc->last_tick = self->last_tick;
            proc->state = PROC_STATE_SWITCHING_CONTEXT;
        })
    End

     __d_println(SCHEDULER, "CPUScheduler__interrupt\n");

    ProcessTable ready = ProcessTable__filter(&self->processes, process_in_state, &ready_state);
    ProcessTable sorted_ready = ProcessTable__sort_desc(&ready, service_priority_then_longest_waiting);

    __d_printf(SCHEDULER, "total num processes - %i\n", ProcessTable__len(&self->processes));
    __d_printf(SCHEDULER, "num ready - %i\n", ProcessTable__len(&sorted_ready));

    Optional(ProcessPtr) selected = ProcessTable__at(&sorted_ready, 0);
    Match(ProcessPtr, selected)
        IsSome(new_proc,
        {
            __d_printf(SCHEDULER, "selected - %p %s\n", new_proc, cstr(new_proc->name));
            new_proc->state = PROC_STATE_SWITCHING_CONTEXT;
            ContextSwitch context_switch = ContextSwitch__new(new_proc, current);

            return Some(ContextSwitch, context_switch);
        });
        IsNone({
            Match(ProcessPtr, current)
                IsSome(proc,
                {
                    // we have a current process context
                    // so restore current to running
                    proc->state = PROC_STATE_RUNNING;
                    __d_printf(SCHEDULER, "no new proc remain with current - %p %s\n", proc, cstr(proc->name));
                    return None(ContextSwitch);
                });
                IsNone(
                {
                    // no current and nothing ready, go idle
                    ProcessPtr idle = self->idle_process;
                    idle->state = PROC_STATE_SWITCHING_CONTEXT;
                    __d_printf(SCHEDULER, "no new proc or current going idle - %p %s\n", idle, cstr(idle->name));
                    ContextSwitch context_switch = ContextSwitch__new(idle, current);
                    return Some(ContextSwitch, context_switch);
                });
            End

        })
    End
    return None(ContextSwitch);
}

static Optional(ContextSwitch) CPUScheduler__yield_waiting(CPUSchedulerPtr self, ProcessPtr waiting_proc)
{
    ProcessState ready_state = PROC_STATE_READY | PROC_STATE_INTERRUPTED;
    ProcessTable ready = ProcessTable__filter(&self->processes, process_in_state, &ready_state);
    ProcessTable sorted_ready = ProcessTable__sort_desc(&ready, service_priority_then_longest_waiting);

    Optional(ProcessPtr) selected = ProcessTable__at(&sorted_ready, 0);
    Match(ProcessPtr, selected)
        IsSome(new_proc,
        {
            new_proc->state = PROC_STATE_SWITCHING_CONTEXT;
            return Some(ContextSwitch, ContextSwitch__new(new_proc, Some(ProcessPtr, waiting_proc)));
        })
        IsNone(
        {
            ProcessPtr idle = self->idle_process;
            idle->state = PROC_STATE_SWITCHING_CONTEXT;
            return Some(ContextSwitch, ContextSwitch__new(idle, Some(ProcessPtr, waiting_proc)));
        })
    End

    return None(ContextSwitch);
}

// __attribute__((noreturn))
void CPUScheduler__start(CPUSchedulerPtr self)
{
    __d_printf(
        SCHEDULER,
        "CPUScheduler__start cpu=%i idle pid=%i name=%s state=%s context=%p page_table=%p\n",
        self->cpu,
        self->idle_process->pid,
        cstr(self->idle_process->name),
        ProcessState__to_string(self->idle_process->state),
        self->idle_process->context,
        paddr(self->idle_process->page_table)
    );
    ContextSwitch context_switch = ContextSwitch__new(self->idle_process, None(ProcessPtr));
    ContextSwitch__switch(context_switch);
}

ProcessTablePtr Scheduler__all_processes(SchedulerPtr self, CPUID cpu)
{
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler,
        {
            return &scheduler->processes;
        });
        IsNone(return NULL);
    End
}

ProcessTable Scheduler__all_processes_for_all_cpus(SchedulerPtr self)
{
    ProcessTable processes = ProcessTable__new();

    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        ProcessTable__push_all(&processes, &sched->processes);
    End

    return processes;
}

ProcessTable Scheduler__process_snapshot(SchedulerPtr self)
{
    ProcessTable processes = ProcessTable__new();

    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        if (sched->started)
        {
            ProcessTable__push(&processes, sched->idle_process);
        }
        ProcessTable__push_all(&processes, &sched->processes);
    End

    return processes;
}

static BString ProcessClass__snapshot_name(ProcessClass process_class)
{
    switch (process_class)
    {
        case PROCESS_CLASS_REALTIME_SERVICE:
            return BSTRING_NEW("rt-service");
        case PROCESS_CLASS_SERVICE:
            return BSTRING_NEW("service");
        case PROCESS_CLASS_NORMAL:
        default:
            return BSTRING_NEW("normal");
    }
}

static BString Process__snapshot_user_name(ProcessPtr process)
{
    if (process->user_id == PROCESS_USER_ROOT)
    {
        return process_user_root;
    }
    if (process->user_id == PROCESS_USER_HUND)
    {
        return process_user_hund;
    }
    if (process->user_id == PROCESS_USER_USER)
    {
        return process_user_user;
    }

    return process_user_unknown;
}

static ProcessInfo ProcessInfo__from_process_locked(ProcessPtr process)
{
    ProcessInfo info = (ProcessInfo) {
        .pid = process->pid,
    };
    BString state = bstring(ProcessState__to_string(process->state));
    BString no_wait_reason = BSTRING_NEW("-");
    BString class_name = ProcessClass__snapshot_name(process->process_class);
    BString user_name = Process__snapshot_user_name(process);

    BString__copy_to(&process->name, info.name, PROCESS_INFO_NAME_SIZE);
    BString__copy_to(&state, info.state, PROCESS_INFO_STATE_SIZE);
    BString__copy_to(&no_wait_reason, info.wait_reason, PROCESS_INFO_WAIT_SIZE);
    Match(ParkReason, Process__top_park_reason(process))
        IsSome(reason,
        {
            BString wait_reason = ParkReason__to_string(reason);
            BString__copy_to(&wait_reason, info.wait_reason, PROCESS_INFO_WAIT_SIZE);
        })
    End
    BString__copy_to(&class_name, info.class_name, PROCESS_INFO_CLASS_SIZE);
    BString__copy_to(&user_name, info.user_name, PROCESS_INFO_USER_SIZE);
    return info;
}

ProcessListResult Scheduler__process_info_snapshot(SchedulerPtr self, ProcessInfoPtr entries, uint64_t max_entries)
{
    uint64_t written = 0;

    CriticalSection(self->scheduler_lock,
    {
        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            if (written >= max_entries)
            {
                _cs_return (ProcessListResult) written;
            }

            if (sched->started)
            {
                entries[written] = ProcessInfo__from_process_locked(sched->idle_process);
                written++;
                if (written >= max_entries)
                {
                    _cs_return (ProcessListResult) written;
                }
            }

            FAIterate(ProcessTable, sched->processes, process_index, process)
                if (written >= max_entries)
                {
                    _cs_return (ProcessListResult) written;
                }
                entries[written] = ProcessInfo__from_process_locked(process);
                written++;
            End
        End
    })

    return (ProcessListResult) written;
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

static bool Scheduler__is_parked_for(ProcessPtr proc, ParkReason reason)
{
    return Process__has_parked_operation(proc, reason, None(OperationKey));
}

static void Scheduler__write_abort_to_stderr(IOSubsystemPtr io_subsystem, ProcessPtr proc, ProcessAbort abort)
{
    if (io_subsystem == NULL)
    {
        return;
    }

    ResultMatch(ProcessIOHandle, Process__get_io_handle(proc, 2))
        IsSuccess(handle,
        {
            ConstBStringPtr reason = ProcessAbortReason__to_string(abort.reason);
            BString message = BString__sprintf(
                &abort_format,
                cstr_p(reason),
                abort.exception_description,
                (int) abort.fault_status_code,
                abort.fault_status_description,
                abort.access_description,
                (unsigned int) abort.pc,
                (unsigned int) abort.fault_address,
                (unsigned int) abort.syndrome,
                (unsigned int) abort.exception_code,
                (int) abort.cpu
            );

            KernelContext kernel_context = KernelContext__new(
                Some(ProcessID, proc->pid), Some(SchedulerPtr, io_subsystem->scheduler)
            );
            ExternalBuffer buffer = ExternalBuffer__new((DataPtr) message.data, message.len);
            IOContextPtr context = IOContext__heap_new(
                kernel_context,
                Some(IOSubsystemPtr, io_subsystem),
                Some(IOHandle, (IOHandle) handle),
                Some(ExternalBuffer, buffer)
            );

            IOSubsystem__write(io_subsystem, context);
            IOContext__dispose(context);
        })
    End
}

static Result(ProcessPtr) Scheduler__terminate_process(SchedulerPtr self, ProcessPtr proc, ReturnCode return_code)
{
    if (is_nothing(self->kernel))
    {
        return Error(ProcessPtr, bstring("Scheduler has no owning kernel"), SCHEDULER_GENERAL_ERROR);
    }

    KernelPtr kernel = self->kernel.some.val;
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__exit(sched, proc);
        ResultMatch(ProcessPtr, res)
            IsSuccess(exited,
            {
                Match(ProcessPtr, exited->parent)
                    IsSome(parent,
                    {
                        if (!Scheduler__is_parked_for(parent, PARK_WAIT_CHILD))
                        {
                            ResultMatch(ServiceCode, Scheduler__notify_parent_child_exit_locked(self, parent, proc, return_code))
                                IsError(code, message, return Error(ProcessPtr, message, code))
                            End
                        }
                        if (Scheduler__is_parked_for(parent, PARK_WAIT_CHILD))
                        {
                            __d_printf(
                                PROGRESS,
                                "scheduler child complete child_pid=%i child_name=%s parent_pid=%i parent_name=%s return_code=%i\n",
                                proc->pid,
                                cstr(proc->name),
                                parent->pid,
                                cstr(parent->name),
                                return_code
                            );
                            ResultMatch(ProcessPtr, Scheduler__unpark(self, parent, PARK_WAIT_CHILD, Some(ReturnCode, return_code)))
                                IsError(code, message, return Error(ProcessPtr, message, code));
                            End
                        }
                    })
                End

                Result(AckCode) publish_result = Scheduler__publish_process_state(self, proc);

                Process__dispose_user_allocations(proc);

                Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                Match(ProcessPtr, current_for_cpu)
                    IsNone(Scheduler__preempt(self, cpu, NULL));
                End

                ResultMatch(AckCode, publish_result)
                    IsError(code, message, return Error(ProcessPtr, message, SCHEDULER_GENERAL_ERROR));
                End

                return res;
            });
        End
    End


    BString no_scheduler_format = bstring("Process not found: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(ServiceCode) Scheduler__notify_parent_child_exit_locked(
    SchedulerPtr self, ProcessPtr parent, ProcessPtr child, ReturnCode return_code
)
{
    ProcessChildEvent event;
    event.pid = child->pid;
    event.status = return_code;
    event.type = PROCESS_CHILD_EVENT_EXITED;

    ResultMatch(CollectionCode, Process__push_child_event(parent, event))
        IsSuccess(_, )
        IsError(code, message, return Error(ServiceCode, message, code))
    End

    return Scheduler__child_mark_runnable_locked(self, parent);
}

Result(ProcessPtr) Scheduler__exit(SchedulerPtr self, ProcessPtr proc, ReturnCode return_code)
{
    return Scheduler__terminate_process(self, proc, return_code);
}

Result(ProcessPtr) Scheduler__abort_process(
    SchedulerPtr self, IOSubsystemPtr io_subsystem, ProcessPtr proc, ProcessAbort abort
)
{
    proc->abort = Some(ProcessAbort, abort);
    Scheduler__write_abort_to_stderr(io_subsystem, proc, abort);
    ReturnCode return_code = PROCESS_ABORT_EXIT_BASE - abort.reason;
    return Scheduler__terminate_process(self, proc, return_code);
}


Result(ProcessPtr) Scheduler__find_process(SchedulerPtr self, ProcessID pid)
{

}

bool pid_matches(ProcessPtr * proc_p, void * pid_p)
{
    ProcessPtr proc = *proc_p;
    ProcessID pid = *(ProcessID *)pid_p;

    __d_printf(SCHEDULER, "pid_matches %i %i\n", proc->pid, pid);
    return proc->pid == pid;
}

Optional(ProcessPtr) Scheduler__find_process_by_pid(SchedulerPtr self, ProcessID pid)
{
    ProcessTable processes = Scheduler__all_processes_for_all_cpus(self);
    __d_printf(SCHEDULER, "Scheduler__find_process_by_pid %i\n", ProcessTable__len(&processes));
    FindResultMatch(ProcessTable, ProcessTable__find(&processes, pid_matches, &pid))
        IsSome(res,
        {
            return Some(ProcessPtr, *res.value);
        })
        IsNone(
        {
            return None(ProcessPtr);
        })
    End
}

Result(ProcessID) Scheduler__park_by_pid(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason
)
{
    return Scheduler__park_by_pid_with_context_ownership_and_operation_key(
        self, pid, kernel_context, kernel_continuation, reason, false, None(OperationKey)
    );
}

Result(ProcessID) Scheduler__park_by_pid_with_context_ownership(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason, bool owns_kernel_context
)
{
    return Scheduler__park_by_pid_with_context_ownership_and_operation_key(
        self, pid, kernel_context, kernel_continuation, reason, owns_kernel_context, None(OperationKey)
    );
}

Result(ProcessID) Scheduler__park_by_pid_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason,
    bool owns_kernel_context, Optional(OperationKey) operation_key
)
{
    Match(ProcessPtr, Scheduler__find_process_by_pid(self, pid))
        IsSome(proc,
        {
            ResultMatch(ProcessPtr, Scheduler__park_with_context_ownership_and_operation_key(
                self, proc, kernel_context, kernel_continuation, reason, owns_kernel_context, operation_key
            ))
                IsSuccess(proc, return Success(ProcessID, pid));
                IsError(code, message, return Error(ProcessID, message, code));
            End
        })
    End
    return Error(ProcessID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}


Result(ProcessPtr) Scheduler__park(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason
)
{
    return Scheduler__park_with_context_ownership_and_operation_key(
        self, proc, kernel_context, kernel_continuation, reason, false, None(OperationKey)
    );
}

Result(ProcessPtr) Scheduler__park_with_context_ownership(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason, bool owns_kernel_context
)
{
    return Scheduler__park_with_context_ownership_and_operation_key(
        self, proc, kernel_context, kernel_continuation, reason, owns_kernel_context, None(OperationKey)
    );
}

static Result(ProcessPtr) Scheduler__park_with_context_ownership_and_operation_key(
    SchedulerPtr self, ProcessPtr proc, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason,
    bool owns_kernel_context, Optional(OperationKey) operation_key
)
{
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__park(
            sched, proc, kernel_context, kernel_continuation, reason, owns_kernel_context, operation_key
        );
        ResultMatch(ProcessPtr, res)
            IsSuccess(exited,
            {
                Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                Match(ProcessPtr, current_for_cpu)
                    IsNone(Scheduler__preempt(self, cpu, NULL));
                End

                return res;
            });
        End
    End


    BString no_scheduler_format = bstring("No scheduler found for process: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}


Result(ProcessID) Scheduler__unpark_by_pid(SchedulerPtr self, ProcessID pid, ParkReason reason, Optional(ReturnCode) return_code)
{
    return Scheduler__unpark_by_pid_with_operation_key(self, pid, reason, return_code, None(OperationKey));
}

Result(ProcessID) Scheduler__unpark_by_pid_with_operation_key(
    SchedulerPtr self, ProcessID pid, ParkReason reason,
    Optional(ReturnCode) return_code, Optional(OperationKey) operation_key
)
{
    Match(ProcessPtr, Scheduler__find_process_by_pid(self, pid))
        IsSome(proc,
        {
            ResultMatch(ProcessPtr, Scheduler__unpark_with_operation_key(self, proc, reason, return_code, operation_key))
                IsSuccess(proc, return Success(ProcessID, pid));
                IsError(code, message, return Error(ProcessID, message, code));
            End
        })
    End
    return Error(ProcessID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessPtr) Scheduler__unpark(SchedulerPtr self, ProcessPtr proc, ParkReason reason, Optional(ReturnCode) return_code)
{
    return Scheduler__unpark_with_operation_key(self, proc, reason, return_code, None(OperationKey));
}

static Result(ProcessPtr) Scheduler__unpark_with_operation_key(
    SchedulerPtr self, ProcessPtr proc, ParkReason reason,
    Optional(ReturnCode) return_code, Optional(OperationKey) operation_key
)
{
    __d_println(SCHEDULER, "scheduler unparking");
    if (!Process__has_parked_operation(proc, reason, operation_key))
    {
        return Error(ProcessPtr, process_wasnt_parked, SCHEDULER_CANT_UNPARK);
    }
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__unpark_with_operation_key(
            sched, proc, reason, return_code, operation_key
        );
        ResultMatch(ProcessPtr, res)
            IsSuccess(unparked,
            {
                __d_println(SCHEDULER, "scheduler unparked process");
                return res;
            });
        End
    End


    BString no_scheduler_format = bstring("Process not found: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(CollectionCode) Scheduler__add_blocked_operation(
    SchedulerPtr self, ParkedOperationPtr operation, ParkedOperationPtr blocked_operation
)
{
    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(CollectionCode, ParkedOperation__add_blocked_operation(operation, blocked_operation))
            IsSuccess(index, _cs_return Success(CollectionCode, index))
            IsError(code, message, _cs_return Error(CollectionCode, message, code))
        End
    })

    return Error(CollectionCode, bstring("Unable to add blocked operation"), LINKED_LIST_ALLOCATION_FAILED);
}

Result(CollectionCode) Scheduler__resolve_blocked_operation(
    SchedulerPtr self, ParkedOperationPtr operation, ParkedOperationPtr blocked_operation
)
{
    Result(CollectionCode) resolved_operation = Error(
        CollectionCode, bstring("Unable to resolve blocked operation"), LINKED_LIST_ITEM_NOT_FOUND
    );

    CriticalSection(self->scheduler_lock,
    {
        resolved_operation = ParkedOperation__resolve_blocked_operation(operation, blocked_operation);
    })

    ResultMatch(CollectionCode, resolved_operation)
        IsSuccess(index,
        {
            ParkedOperation__dispose(blocked_operation);
            return Success(CollectionCode, index);
        })
        IsError(code, message, return Error(CollectionCode, message, code))
    End

    return Error(CollectionCode, bstring("Unable to resolve blocked operation"), LINKED_LIST_ITEM_NOT_FOUND);
}

bool Scheduler__parked_operation_is_ready(SchedulerPtr self, ParkedOperationPtr operation)
{
    bool ready = false;
    CriticalSection(self->scheduler_lock, ready = ParkedOperation__is_ready(operation);)
    return ready;
}


Result(SchedulerInitStatus) Scheduler__init(KernelPtr kernel, SchedulerPtr self, Elf idle_process)
{
    __d_println(SCHEDULER, "Scheduler__init");
    self->kernel = Some(KernelPtr, kernel);
    uint64_t cpu_count = MAX_CPUS;
    Match(SystemInfo, kernel->system_info)
        IsSome(system_info,
        {
            self->interrupt_controller = Some(InterruptControllerInfo, system_info.interrupt_controller);
            cpu_count = system_info.cpu_count;
        })
    End
    if (cpu_count == 0)
    {
        return Error(SchedulerInitStatus, no_cpus_available, SCHEDULER_INIT_ERROR);
    }
    if (cpu_count > MAX_CPUS)
    {
        cpu_count = MAX_CPUS;
    }
    self->ready = false;
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        __d_printf(SCHEDULER, "Scheduler__init cpu - %i\n", i);
        ResultMatch(CPUSchedulerPtr, CPUScheduler__heap_new(kernel, idle_process, i))
            IsError(code, message,
            {
                return Error(SchedulerInitStatus, message, SCHEDULER_INIT_ERROR);
            });
            IsSuccess(cpu_scheduler,
            {
                __d_printf(SCHEDULER, "Scheduler__init adding cpu sched - %i\n", i);
                CPUSchedulers__push(&self->schedulers, cpu_scheduler);
            });
        End
    }
    return Success(SchedulerInitStatus, SCHEDULER_INIT_OK);
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

Optional(ProcessPtr) Scheduler__current_process(SchedulerPtr self, CPUID cpu)
{
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler,
        {
            return CPUScheduler__current_process(scheduler);
        });
    End
    return None(ProcessPtr);
}

bool Scheduler__should_preempt_on_timer(SchedulerPtr self, CPUID cpu)
{
    Match(ProcessPtr, Scheduler__current_process(self, cpu))
        IsSome(process, return !Process__is_handling_service_irq(process))
    End

    return true;
}

Optional(ProcessPtr) Scheduler__save_context(SchedulerPtr self, CPUID cpu, ProcessContextPtr context)
{
    Match(ProcessPtr, Scheduler__current_process(self, cpu))
        IsSome(proc,
        {
            __d_printf(SCHEDULER, "Scheduler__save_context %s\n", cstr(proc->name));
            Process__save_context(proc, context);
            return Some(ProcessPtr, proc);
        });
    End
    return None(ProcessPtr);
}

Optional(ProcessPtr) Scheduler__idle_process(SchedulerPtr self, CPUID cpu)
{
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler, return Some(ProcessPtr, scheduler->idle_process));
    End
    return None(ProcessPtr);
}

SchedulerPtr Scheduler__heap_new(MessageBrokerPtr message_broker)
{
    SchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(Scheduler)), Scheduler);
    *sched = NEW_SCHEDULER(message_broker);
    return sched;
}

Result(CPUID) Scheduler__schedule(SchedulerPtr self, ProcessPtr new_proc)
{
    // just round robin the cpus
    ProcessID pid = new_proc->pid;
    CPUID cpu = (pid % CPU__count());
    return Scheduler__schedule_on_cpu(self, new_proc, cpu);
}

Result(CPUID) Scheduler__schedule_on_cpu(SchedulerPtr self, ProcessPtr new_proc, CPUID cpu)
{
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler,
        {
            return CPUScheduler__schedule(scheduler, new_proc);
        })
        IsNone(
        {
            BString invalid_cpu_format = bstring("Invalid CPU %i\n");
            BString invalid_cpu = BString__sprintf(&invalid_cpu_format, cpu);

            return Error(CPUID, invalid_cpu, SCHEDULER_INVALID_CPU);
        });
    End

    return Error(CPUID, unreachable_code, SCHEDULER_GENERAL_ERROR);
}

Result(CPUID) Scheduler__cpu_for_process(SchedulerPtr self, ProcessPtr process)
{
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Match(ArrayIndex, ProcessTable__find_item(&sched->processes, process))
            IsSome(index, return Success(CPUID, cpu))
        End
    End

    return Error(CPUID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ServiceCode) Scheduler__subscribe_irq(SchedulerPtr self, ProcessPtr process, IRQNumber irq)
{
    CriticalSection(self->scheduler_lock,
    {
        _cs_return ServiceEventDispatcher__subscribe_irq(&self->service_event_dispatcher, process, irq);
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__send_irq_event(SchedulerPtr self, IRQNumber irq)
{
    __d_printf(IODEV, "scheduler send irq event irq=%i\n", irq);
    if (is_nothing(self->interrupt_controller))
    {
        return Error(ServiceCode, missing_interrupt_controller, SERVICE_SCHEDULER_ERROR);
    }

    InterruptControllerInfoPtr interrupt_controller = &self->interrupt_controller.some.val;
    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(ServiceEventDispatch, ServiceEventDispatcher__dispatch_irq(
            &self->service_event_dispatcher, interrupt_controller, irq
        ))
            IsSuccess(dispatch,
            {
                Match(ProcessPtr, dispatch.process)
                    IsSome(process,
                    {
                        ResultMatch(ServiceCode, Scheduler__service_mark_runnable_locked(self, process))
                            IsError(code, message, _cs_return Error(ServiceCode, message, code))
                        End
                    })
                End
            })
            IsError(code, message, _cs_return Error(ServiceCode, message, code))
        End
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEvent) Scheduler__service_take_next_event(SchedulerPtr self, ProcessPtr process)
{
    CriticalSection(self->scheduler_lock,
    {
        _cs_return ServiceEventDispatcher__take_next(&self->service_event_dispatcher, process);
    })

    return Error(ServiceEvent, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ProcessChildEvent) Scheduler__child_take_next_event(SchedulerPtr self, ProcessPtr process)
{
    CriticalSection(self->scheduler_lock,
    {
        _cs_return Process__pop_next_child_event(process);
    })

    return Error(ProcessChildEvent, child_event_queue_empty, PROCESS_CHILD_EVENT_QUEUE_EMPTY);
}

Result(ServiceEvent) Scheduler__service_wait(
    SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context, Optional(Continuation) continuation
)
{
    return Scheduler__service_wait_with_context_ownership(
        self, process, kernel_context, continuation, false
    );
}

Result(ServiceEvent) Scheduler__service_wait_with_context_ownership(
    SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context,
    Optional(Continuation) continuation, bool owns_kernel_context
)
{
    if (is_nothing(self->interrupt_controller))
    {
        return Error(ServiceEvent, missing_interrupt_controller, SERVICE_SCHEDULER_ERROR);
    }

    InterruptControllerInfoPtr interrupt_controller = &self->interrupt_controller.some.val;
    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(ServiceCode, ServiceEventDispatcher__complete_active_irq(
            &self->service_event_dispatcher, interrupt_controller, process
        ))
            IsError(code, message, _cs_return Error(ServiceEvent, message, code))
        End

        ResultMatch(ServiceEvent, ServiceEventDispatcher__take_next(&self->service_event_dispatcher, process))
            IsSuccess(event, _cs_return Success(ServiceEvent, event))
            IsError(code, message,
            {
                if (code != SERVICE_EVENT_QUEUE_EMPTY)
                {
                    _cs_return Error(ServiceEvent, message, code);
                }
            })
        End

        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            ResultMatch(ProcessPtr, CPUScheduler__park(
                sched, process, kernel_context, continuation,
                PARK_WAIT_SERVICE_EVENT, owns_kernel_context, None(OperationKey)
            ))
                IsSuccess(parked, _cs_return Error(ServiceEvent, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY))
            End
        End
    })

    BString no_scheduler_format = bstring("No scheduler found for process: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, process->pid);
    return Error(ServiceEvent, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessChildEvent) Scheduler__child_wait_with_context_ownership(
    SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context,
    Optional(Continuation) continuation, bool owns_kernel_context
)
{
    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(ProcessChildEvent, Process__pop_next_child_event(process))
            IsSuccess(event, _cs_return Success(ProcessChildEvent, event))
            IsError(code, message,
            {
                if (code != PROCESS_CHILD_EVENT_QUEUE_EMPTY)
                {
                    _cs_return Error(ProcessChildEvent, message, code);
                }
            })
        End

        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            ResultMatch(ProcessPtr, CPUScheduler__park(
                sched, process, kernel_context, continuation,
                PARK_WAIT_CHILD_EVENT, owns_kernel_context, None(OperationKey)
            ))
                IsSuccess(parked, _cs_return Error(ProcessChildEvent, child_event_queue_empty, PROCESS_CHILD_EVENT_QUEUE_EMPTY))
            End
        End
    })

    BString no_scheduler_format = bstring("No scheduler found for process: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, process->pid);
    return Error(ProcessChildEvent, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(ServiceCode) Scheduler__service_mark_runnable_locked(SchedulerPtr self, ProcessPtr process)
{
    if (Scheduler__is_parked_for(process, PARK_WAIT_SERVICE_EVENT))
    {
        __d_printf(IODEV, "service enqueue unparking pid=%i\n", process->pid);
        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            ResultMatch(ProcessPtr, CPUScheduler__unpark(sched, process, PARK_WAIT_SERVICE_EVENT, None(ReturnCode)))
                IsSuccess(unparked,
                {
                    __d_printf(
                        IODEV,
                        "service enqueue unparked pid=%i cpu=%i state=%s\n",
                        process->pid,
                        cpu,
                        ProcessState__to_string(process->state)
                    );
                    return Success(ServiceCode, SERVICE_OK);
                })
                IsError(code, message,
                {
                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                    {
                        return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR);
                    }
                })
            End
        End
    }

    if (process->state == PROC_STATE_WAITING && ServiceEvents__len(&process->service.events) > 0)
    {
        process->state = PROC_STATE_INTERRUPTED;
    }

    return Success(ServiceCode, SERVICE_OK);

    BString no_scheduler_format = bstring("No scheduler found for process: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, process->pid);
    return Error(ServiceCode, no_scheduler, SERVICE_SCHEDULER_ERROR);
}

Result(ServiceCode) Scheduler__service_enqueue(SchedulerPtr self, ProcessPtr process, ServiceEvent event)
{
    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(ServiceCode, ServiceEventDispatcher__enqueue(&self->service_event_dispatcher, process, event))
            IsError(code, message, _cs_return Error(ServiceCode, message, code))
        End

        _cs_return Scheduler__service_mark_runnable_locked(self, process);
    })

    return Success(ServiceCode, SERVICE_OK);
}

static Result(ServiceCode) Scheduler__service_ready_mark_runnable_locked(
    SchedulerPtr self, ProcessPtr process, ServiceCode result
)
{
    if (Scheduler__is_parked_for(process, PARK_WAIT_SERVICE_READY))
    {
        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            ResultMatch(ProcessPtr, CPUScheduler__unpark(
                sched, process, PARK_WAIT_SERVICE_READY, Some(ReturnCode, result)
            ))
                IsSuccess(unparked, return Success(ServiceCode, SERVICE_OK))
                IsError(code, message,
                {
                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                    {
                        return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR);
                    }
                })
            End
        End
    }

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__service_publish_ready(SchedulerPtr self, ConstBStringPtr name)
{
    CriticalSection(self->scheduler_lock,
    {
        ServiceReadyProcesses ready_processes = ServiceReadyProcesses__new();
        ResultMatch(ServiceCode, ServiceEventDispatcher__publish_ready(
            &self->service_event_dispatcher, name, &ready_processes
        ))
            IsError(code, message, _cs_return Error(ServiceCode, message, code))
        End

        FAIterate(ServiceReadyProcesses, ready_processes, index, process)
            ResultMatch(ServiceCode, Scheduler__service_ready_mark_runnable_locked(self, process, SERVICE_OK))
                IsError(code, message, _cs_return Error(ServiceCode, message, code))
            End
        End
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__service_wait_ready_with_context_ownership(
    SchedulerPtr self, ProcessPtr process, ConstBStringPtr name,
    KernelContextPtr kernel_context, Optional(Continuation) continuation, bool owns_kernel_context
)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceCode, service_event_queue_empty, SERVICE_NOT_SERVICE_PROCESS);
    }

    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(ServiceCode, ServiceEventDispatcher__wait_ready(
            &self->service_event_dispatcher, process, name
        ))
            IsSuccess(code,
            {
                if (code == SERVICE_OK && ServiceEventDispatcher__is_ready(&self->service_event_dispatcher, name))
                {
                    _cs_return Success(ServiceCode, SERVICE_OK);
                }

                FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
                    ResultMatch(ProcessPtr, CPUScheduler__park(
                        sched, process, kernel_context, continuation,
                        PARK_WAIT_SERVICE_READY, owns_kernel_context, None(OperationKey)
                    ))
                        IsSuccess(parked, _cs_return Error(ServiceCode, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY))
                    End
                End
            })
            IsError(code, message, _cs_return Error(ServiceCode, message, code))
        End
    })

    BString no_scheduler_format = bstring("No scheduler found for process: %i\n");
    BString no_scheduler = BString__sprintf(&no_scheduler_format, process->pid);
    return Error(ServiceCode, no_scheduler, SERVICE_SCHEDULER_ERROR);
}

static Result(ServiceCode) Scheduler__child_mark_runnable_locked(SchedulerPtr self, ProcessPtr process)
{
    if (Scheduler__is_parked_for(process, PARK_WAIT_CHILD_EVENT))
    {
        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            ResultMatch(ProcessPtr, CPUScheduler__unpark(sched, process, PARK_WAIT_CHILD_EVENT, None(ReturnCode)))
                IsSuccess(unparked, return Success(ServiceCode, SERVICE_OK))
                IsError(code, message,
                {
                    if (code != SCHEDULER_PROCESS_NOT_FOUND)
                    {
                        return Error(ServiceCode, message, code);
                    }
                })
            End
        End
    }

    return Success(ServiceCode, SERVICE_OK);
}


Optional(ContextSwitch) Scheduler__interrupt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context)
{
    CriticalSection(self->scheduler_lock,
    {
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler,
            {
                if (!scheduler->started)
                {
                    _cs_return None(ContextSwitch);
                }
                __d_printf(SCHEDULER, "CPUScheduler__interrupt %i\n", cpu);
                _cs_return CPUScheduler__interrupt(scheduler, context);
            });
            IsNone(
            {
                __d_printf(PANIC, "Invalid CPU %i\n", cpu);
                _cs_return None(ContextSwitch);
            });
        End

    });

    return None(ContextSwitch);
}


void Scheduler__preempt(SchedulerPtr self, CPUID cpu, ProcessContextPtr context)
{
    Optional(ContextSwitch) context_switch = Scheduler__interrupt(self, cpu, context);
    Match(ContextSwitch, context_switch)
        IsSome(context, ContextSwitch__switch(context))
    End

}

void Scheduler__yield_waiting(SchedulerPtr self, ProcessPtr waiting_proc, CPUID cpu)
{
    Optional(ContextSwitch) context_switch = None(ContextSwitch);
    __d_printf(
        IODEV,
        "Scheduler__yield_waiting cpu=%i waiting pid=%i name=%s\n",
        cpu,
        waiting_proc->pid,
        cstr(waiting_proc->name)
    );
    CriticalSection(self->scheduler_lock,
    {
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler, context_switch = CPUScheduler__yield_waiting(scheduler, waiting_proc))
        End
    })
    Match(ContextSwitch, context_switch)
        IsSome(next,
        {
            __d_printf(
                IODEV,
                "Scheduler__yield_waiting switch cpu=%i from pid=%i to pid=%i name=%s\n",
                cpu,
                waiting_proc->pid,
                next.new_proc->pid,
                cstr(next.new_proc->name)
            );
            ContextSwitch__switch(next);
        })
        IsNone(__d_printf(IODEV, "Scheduler__yield_waiting no switch cpu=%i pid=%i\n", cpu, waiting_proc->pid))
    End
}


Result(SchedulerInitStatus) Scheduler__start(SchedulerPtr self, CPUID cpu)
{
    __d_printf(SCHEDULER, "Scheduler__start cpu=%i\n", cpu);
    // we just spawn the idle process and new process starts will rely on the queues
    CriticalSection(self->scheduler_lock,
    {
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler,
            {
                __d_printf(
                    SCHEDULER,
                    "Scheduler__start found cpu=%i scheduler=%p idle=%p started=%i\n",
                    cpu,
                    scheduler,
                    scheduler->idle_process,
                    scheduler->started
                );
                // will never return from here
                scheduler->started = true;
                _cs_exit_then(CPUScheduler__start(scheduler));
            });
            IsNone(
            {
                BString invalid_cpu_format = bstring("Invalid CPU %i\n");
                BString invalid_cpu = BString__sprintf(&invalid_cpu_format, cpu);

                _cs_return Error(SchedulerInitStatus, invalid_cpu, SCHEDULER_INVALID_CPU);
                __d_println(MUTEX, "Unreachable code reached");
            });
        End
    });

    BString scheduler_failed_format = bstring("Scheduler failed to start CPU %i\n");

    return Error(SchedulerInitStatus, BString__sprintf(&scheduler_failed_format, cpu), SCHEDULER_INIT_ERROR);
}

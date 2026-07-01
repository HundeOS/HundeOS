#include "scheduler.h"
#include "../arch/process.h"
#include "../types/result.h"
#include "../types/optional.h"
#include "../mutex.h"
#include "../kalloc.h"
#include "../print.h"
#include "../all_defs.h"
#include "../spin.h"
#include "parked.h"
#include "../subsystems/message/subsystem_codes.h"
#include "../subsystems/io/io_subsystem.h"
#include "../service/service.h"
#include "../arch/init.h"

FixedArrayBodyDefine(CPUSchedulers);
MapBodyDefine(IRQServiceMap);

#define CPU_SCHEDULER_INIT(idle, cpu) \
    (CPUScheduler) { \
        .idle_process = idle, \
        .processes = EmptyArray(ProcessTable), \
        .scheduler_lock = MUTEX_INIT, \
        .last_tick = 0, \
        .cpu = cpu, \
        .started = false, \
    }

KString unreachable_code = KSTRING_NEW("Unreachable code reached\n");
KString process_not_found = KSTRING_NEW("Process not found\n");
KString reason_mismatch_format = KSTRING_NEW("Process was parked for reason %i, not %i\n");
KString process_wasnt_parked = KSTRING_NEW("Process wasn't parked\n");
static KString abort_format = KSTRING_NEW(
    "process fault: %s exception=%s fsc=%i:%s access=%s pc=0x%x far=0x%x esr=0x%x ec=0x%x cpu=%i\n"
);
static const KString data_abort_reason = KSTRING_NEW("data abort");
static const KString instruction_abort_reason = KSTRING_NEW("instruction abort");
static const KString undefined_instruction_reason = KSTRING_NEW("undefined instruction");
static const KString alignment_fault_reason = KSTRING_NEW("alignment fault");
static const KString bad_syscall_reason = KSTRING_NEW("bad syscall");
static const KString unknown_user_exception_reason = KSTRING_NEW("unknown user exception");
static const KString unknown_fault_reason = KSTRING_NEW("unknown fault");
static KString service_event_queue_empty = KSTRING_NEW("Service event queue is empty");

static Result(ServiceCode) Scheduler__complete_pending_irqs_for_process_locked(SchedulerPtr self, ProcessPtr process);
static Result(ServiceCode) Scheduler__service_enqueue_locked(SchedulerPtr self, ProcessPtr process, ServiceEvent event);

Result(CPUSchedulerPtr) CPUScheduler__heap_new(Elf idle_process, CPUID cpu)
{
    ResultMatch(ProcessPtr, Process__setup(idle_process, None(ProcessPtr)))
        IsSuccess(idle,
        {
            CPUSchedulerPtr sched = v_ptr(Kernel__alloc(sizeof(CPUScheduler)), CPUScheduler);
            *sched = CPU_SCHEDULER_INIT(idle, cpu);
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
    Optional(Continuation) kernel_continuation, ParkReason reason
)
{
    __d_printf(SCHEDULER, "CPUScheduler__park %s\n", cstr(proc->name));
    Match(ArrayIndex, ProcessTable__find_item(&self->processes, proc))
        IsSome(index,
        {
            ParkedKernelContextPtr parked = ParkedKernelContext__heap_new(kernel_context, kernel_continuation, reason);
            proc->state = PROC_STATE_WAITING;
            __d_printf(SCHEDULER, "CPUScheduler__park - context %i\n", parked);
            ResultMatch(CollectionCode, Process__push_parked_context(proc, parked))
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
            Match(ParkedKernelContextPtr, Process__top_parked_context(proc))
                IsSome(parked_context,
                {
                    if (parked_context->park_reason == reason)
                    {
                        proc->state = PROC_STATE_INTERRUPTED;
                        parked_context->return_code = return_code;
                        __d_printf(
                            SCHEDULER,
                            "CPUScheduler__unpark cpu=%i success pid=%i state=%s\n",
                            self->cpu,
                            proc->pid,
                            ProcessState__to_string(proc->state)
                        );
                        return Success(ProcessPtr, proc);
                    }
                    __d_printf(
                        SCHEDULER,
                        "CPUScheduler__unpark cpu=%i reason mismatch pid=%i wanted=%i parked=%i\n",
                        self->cpu,
                        proc->pid,
                        reason,
                        parked_context->park_reason
                    );
                    return Error(ProcessPtr, KString__sprintf(&reason_mismatch_format, reason, parked_context), SCHEDULER_CANT_UNPARK);
                })
                IsNone(
                {
                    __d_printf(SCHEDULER, "CPUScheduler__unpark cpu=%i target pid=%i was not parked\n", self->cpu, proc->pid);
                    return Error(ProcessPtr, process_wasnt_parked, SCHEDULER_CANT_UNPARK);
                })
            End

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
    ContextSwitch__switch(Some(ContextSwitch, context_switch));
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

static ConstKStringPtr ProcessAbortReason__to_string(ProcessAbortReason reason)
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
    Match(ParkedKernelContextPtr, Process__top_parked_context(proc))
        IsSome(parked_context,
        {
            return parked_context->park_reason == reason;
        })
    End

    return false;
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
            ConstKStringPtr reason = ProcessAbortReason__to_string(abort.reason);
            KString message = KString__sprintf(
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
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__exit(sched, proc);
        ResultMatch(ProcessPtr, res)
            IsSuccess(exited,
            {
                Match(ProcessPtr, exited->parent)
                    IsSome(parent,
                    {
                        if (Scheduler__is_parked_for(parent, PARK_WAIT_CHILD))
                        {
                            ResultMatch(ProcessPtr, Scheduler__unpark(self, parent, PARK_WAIT_CHILD, Some(ReturnCode, return_code)))
                                IsError(code, message, return Error(ProcessPtr, message, code));
                            End
                        }
                    })
                End

                MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, proc->state, proc->pid);
                MessageBroker__deliver(self->message_broker, message);

                Process__dispose_user_allocations(proc);

                Optional(ProcessPtr) current_for_cpu = CPUScheduler__current_process(sched);

                Match(ProcessPtr, current_for_cpu)
                    IsNone(Scheduler__preempt(self, cpu, NULL));
                End

                return res;
            });
        End
    End


    KString no_scheduler_format = kstring("Process not found: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
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
    Match(ProcessPtr, Scheduler__find_process_by_pid(self, pid))
        IsSome(proc,
        {
            ResultMatch(ProcessPtr, Scheduler__park(self, proc, kernel_context, kernel_continuation, reason))
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
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__park(sched, proc, kernel_context, kernel_continuation, reason);
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


    KString no_scheduler_format = kstring("No scheduler found for process: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}


Result(ProcessID) Scheduler__unpark_by_pid(SchedulerPtr self, ProcessID pid, ParkReason reason, Optional(ReturnCode) return_code)
{
    Match(ProcessPtr, Scheduler__find_process_by_pid(self, pid))
        IsSome(proc,
        {
            ResultMatch(ProcessPtr, Scheduler__unpark(self, proc, reason, return_code))
                IsSuccess(proc, return Success(ProcessID, pid));
                IsError(code, message, return Error(ProcessID, message, code));
            End
        })
    End
    return Error(ProcessID, process_not_found, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessPtr) Scheduler__unpark(SchedulerPtr self, ProcessPtr proc, ParkReason reason, Optional(ReturnCode) return_code)
{
    __d_println(SCHEDULER, "scheduler unparking");
    if (!Scheduler__is_parked_for(proc, reason))
    {
        return Error(ProcessPtr, process_wasnt_parked, SCHEDULER_CANT_UNPARK);
    }
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        Result(ProcessPtr) res = CPUScheduler__unpark(sched, proc, reason, return_code);
        ResultMatch(ProcessPtr, res)
            IsSuccess(unparked,
            {
                __d_println(SCHEDULER, "scheduler unparked process");
                return res;
            });
        End
    End


    KString no_scheduler_format = kstring("Process not found: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, proc->pid);
    return Error(ProcessPtr, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}


Result(SchedulerInitStatus) Scheduler__init(SchedulerPtr self, Elf idle_process)
{
    __d_println(SCHEDULER, "Scheduler__init");
    self->ready = false;
    for (uint64_t i = 0; i < MAX_CPUS; i++)
    {
        __d_printf(SCHEDULER, "Scheduler__init cpu - %i\n", i);
        ResultMatch(CPUSchedulerPtr, CPUScheduler__heap_new(idle_process, i))
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
    Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
        IsSome(scheduler,
        {
            return CPUScheduler__schedule(scheduler, new_proc);
        })
        IsNone(
        {
            KString invalid_cpu_format = kstring("Invalid CPU %i\n");
            KString invalid_cpu = KString__sprintf(&invalid_cpu_format, cpu);

            return Error(CPUID, invalid_cpu, SCHEDULER_INVALID_CPU);
        });
    End

    return Error(CPUID, unreachable_code, SCHEDULER_GENERAL_ERROR);
}

Result(ServiceCode) Scheduler__subscribe_irq(SchedulerPtr self, ProcessPtr process, IRQNumber irq)
{
    __d_printf(IODEV, "scheduler subscribe irq=%i pid=%i name=%s\n", irq, process->pid, cstr(process->name));
    IRQService service = (IRQService) {
        .process = process,
        .pending = false,
        .enabled = true,
        .delivery_count = 0,
        .coalesced_count = 0,
    };
    CriticalSection(self->scheduler_lock,
    {
        IRQServiceMap__set(&self->irq_services, irq, service);
    })
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__send_irq_event(SchedulerPtr self, IRQNumber irq)
{
    __d_printf(IODEV, "scheduler send irq event irq=%i\n", irq);
    CriticalSection(self->scheduler_lock,
    {
        MapPtrMatch(IRQServiceMap, IRQServiceMap__get_ptr(&self->irq_services, irq))
            IsSome(service,
            {
                __d_printf(
                    IODEV,
                    "scheduler irq service irq=%i pid=%i pending=%i enabled=%i delivery=%i coalesced=%i\n",
                    irq,
                    service->process->pid,
                    (int) service->pending,
                    (int) service->enabled,
                    (int) service->delivery_count,
                    (int) service->coalesced_count
                );
                if (!service->enabled)
                {
                    _cs_return Success(ServiceCode, SERVICE_OK);
                }

                if (service->pending)
                {
                    service->coalesced_count++;
                    _cs_return Success(ServiceCode, SERVICE_OK);
                }

                service->pending = true;
                service->delivery_count++;
                Arch__mask_irq(irq);

                ServiceEvent event;
                event.type = SERVICE_EVENT_IRQ;
                event.source = irq;
                event.data = service->coalesced_count;
                _cs_return Scheduler__service_enqueue_locked(self, service->process, event);
            })
        End
    })
    __d_printf(IODEV, "scheduler no irq service irq=%i\n", irq);
    return Success(ServiceCode, SERVICE_OK);
}

static Result(ServiceCode) Scheduler__complete_pending_irqs_for_process_locked(SchedulerPtr self, ProcessPtr process)
{
    for (uint64_t index = 0; index < self->irq_services.len; index++)
    {
        IRQNumber irq = self->irq_services.entries[index].key;
        IRQServicePtr service = &self->irq_services.entries[index].value;
        if (service->process == process && service->pending)
        {
            __d_printf(IODEV, "scheduler complete pending irq=%i pid=%i\n", irq, process->pid);
            service->pending = false;
            service->coalesced_count = 0;
            Arch__unmask_irq(irq);
        }
    }
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) Scheduler__complete_pending_irqs_for_process(SchedulerPtr self, ProcessPtr process)
{
    CriticalSection(self->scheduler_lock,
    {
        _cs_return Scheduler__complete_pending_irqs_for_process_locked(self, process);
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEvent) Scheduler__service_wait(
    SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context, Optional(Continuation) continuation
)
{
    CriticalSection(self->scheduler_lock,
    {
        ResultMatch(ServiceEvent, Service__pop_next_event(process))
            IsSuccess(event, _cs_return Success(ServiceEvent, event))
            IsError(code, message,
            {
                if (code != SERVICE_EVENT_QUEUE_EMPTY)
                {
                    _cs_return Error(ServiceEvent, message, code);
                }
            })
        End

        ResultMatch(ServiceCode, Scheduler__complete_pending_irqs_for_process_locked(self, process))
            IsError(code, message, _cs_return Error(ServiceEvent, message, code))
        End

        FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
            ResultMatch(ProcessPtr, CPUScheduler__park(sched, process, kernel_context, continuation, PARK_WAIT_SERVICE_EVENT))
                IsSuccess(parked, _cs_return Error(ServiceEvent, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY))
            End
        End
    })

    KString no_scheduler_format = kstring("No scheduler found for process: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, process->pid);
    return Error(ServiceEvent, no_scheduler, SCHEDULER_PROCESS_NOT_FOUND);
}

static Result(ServiceCode) Scheduler__service_enqueue_locked(SchedulerPtr self, ProcessPtr process, ServiceEvent event)
{
    __d_printf(
        IODEV,
        "service enqueue pid=%i name=%s type=%i source=%i data=%i queued=%i\n",
        process->pid,
        cstr(process->name),
        event.type,
        event.source,
        event.data,
        (int) ServiceEvents__len(&process->service_events)
    );

    ResultMatch(ServiceCode, Service__push_next_event(process, event))
        IsError(code, message, return Error(ServiceCode, message, code))
    End

    if (!Scheduler__is_parked_for(process, PARK_WAIT_SERVICE_EVENT))
    {
        return Success(ServiceCode, SERVICE_OK);
    }

    __d_printf(IODEV, "service enqueue unparking pid=%i type=%i source=%i\n", process->pid, event.type, event.source);
    FAIterate(CPUSchedulers, self->schedulers, cpu, sched)
        ResultMatch(ProcessPtr, CPUScheduler__unpark(sched, process, PARK_WAIT_SERVICE_EVENT, None(ReturnCode)))
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

    KString no_scheduler_format = kstring("No scheduler found for process: %i\n");
    KString no_scheduler = KString__sprintf(&no_scheduler_format, process->pid);
    return Error(ServiceCode, no_scheduler, SERVICE_SCHEDULER_ERROR);
}

Result(ServiceCode) Scheduler__service_enqueue(SchedulerPtr self, ProcessPtr process, ServiceEvent event)
{
    CriticalSection(self->scheduler_lock,
    {
        _cs_return Scheduler__service_enqueue_locked(self, process, event);
    })

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
    ContextSwitch__switch(context_switch);
}

void Scheduler__yield_waiting(SchedulerPtr self, ProcessPtr waiting_proc, CPUID cpu)
{
    Optional(ContextSwitch) context_switch = None(ContextSwitch);
    CriticalSection(self->scheduler_lock,
    {
        Match(CPUSchedulerPtr, CPUSchedulers__at(&self->schedulers, cpu))
            IsSome(scheduler, context_switch = CPUScheduler__yield_waiting(scheduler, waiting_proc))
            IsNone(context_switch = None(ContextSwitch))
        End
    })
    ContextSwitch__switch(context_switch);
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
                KString invalid_cpu_format = kstring("Invalid CPU %i\n");
                KString invalid_cpu = KString__sprintf(&invalid_cpu_format, cpu);

                _cs_return Error(SchedulerInitStatus, invalid_cpu, SCHEDULER_INVALID_CPU);
                __d_println(MUTEX, "Unreachable code reached");
            });
        End
    });

    KString scheduler_failed_format = kstring("Scheduler failed to start CPU %i\n");

    return Error(SchedulerInitStatus, KString__sprintf(&scheduler_failed_format, cpu), SCHEDULER_INIT_ERROR);
}

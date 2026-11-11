#include "preferred.h"
#include "shared_wait.h"
#include "../debug.h"

static bool Process__schedulable(ProcessPtr self)
{
    return self->thread.state == PROC_STATE_READY || self->thread.state == PROC_STATE_INTERRUPTED;
}

static bool Process__has_ready_exec_continuation(ProcessPtr self)
{
    return Process__has_ready_parked_operation(self, PARK_WAIT_EXEC_LOAD, &None(OperationKey));
}

static int Process__preferred_priority(ProcessPtr * first, ProcessPtr * second)
{
    bool first_exec = Process__has_ready_exec_continuation(*first);
    bool second_exec = Process__has_ready_exec_continuation(*second);
    if (first_exec != second_exec)
    {
        return first_exec ? 1 : -1;
    }

    bool first_pending = Process__has_pending_messages(*first);
    bool second_pending = Process__has_pending_messages(*second);
    if (first_pending != second_pending)
    {
        return first_pending ? 1 : -1;
    }

    if ((*first)->thread.preferred_dependency_weight != (*second)->thread.preferred_dependency_weight)
    {
        return (*first)->thread.preferred_dependency_weight > (*second)->thread.preferred_dependency_weight ? 1 : -1;
    }

    ProcessPriority first_priority = ProcessClass__priority((*first)->scheduling.process_class);
    ProcessPriority second_priority = ProcessClass__priority((*second)->scheduling.process_class);
    if (first_priority != second_priority)
    {
        return first_priority > second_priority ? 1 : -1;
    }

    if ((*first)->thread.last_tick != (*second)->thread.last_tick)
    {
        return (*first)->thread.last_tick < (*second)->thread.last_tick ? 1 : -1;
    }
    if ((*first)->pid == (*second)->pid)
    {
        return 0;
    }
    return (*first)->pid < (*second)->pid ? 1 : -1;
}

static void Process__observe_preferred_ticket(ProcessPtr self, ConstRequestTicketPtr ticket)
{
    bool already_observed = false;
    match_optional(
        self->thread.preferred_ticket,
        some(current)
        {
            already_observed = RequestTicket__equals(&current, ticket);
        },
        none_do_nothing()
    );
    if (already_observed)
    {
        return;
    }
    self->thread.preferred_ticket = Some(RequestTicket, *ticket);
    self->thread.preferred_eligible_at_usec = CPU__time_usec();
    self->thread.preferred_selected_at_usec = 0;
    self->thread.preferred_dispatched_at_usec = 0;
    self->thread.preferred_trace_pending = true;
}

static bool Scheduler__enqueue_preferred(SchedulerPtr self, ProcessPtr process, bool warm,
                                         const OptionalPtr(RequestTicket) ticket)
{
    match_optional(*ticket,
        some(value)
        {
            Process__observe_preferred_ticket(process, &value);
        },
        none_do_nothing()
    );
    if (process->thread.state == PROC_STATE_RUNNING)
    {
        process->thread.preferred_active = true;
        process->thread.preferred_warm = warm;
        match_optional(
            process->thread.preferred_ticket,
            some(value)
            {
                if (process->thread.preferred_trace_pending)
                {
                    uint64_t now = CPU__time_usec();
                    process->thread.preferred_selected_at_usec = now;
                    process->thread.preferred_dispatched_at_usec = now;
                    Process__publish_dispatch_trace(process, &value, process->thread.preferred_eligible_at_usec, now,
                                                    now);
                    process->thread.preferred_trace_pending = false;
                }
            },
            none_do_nothing()
        );
        return true;
    }
    if (process->thread.state == PROC_STATE_SWITCHING_CONTEXT)
    {
        return true;
    }
    if (!Process__schedulable(process) || (!warm && !Process__shared_wait_ready(process)))
    {
        return true;
    }
    match_optional(
        ProcessTable__find_item(&self->preferred, process),
        some(index)
        {
            if (warm)
            {
                process->thread.preferred_warm = true;
                process->thread.preferred_ticket = *ticket;
            }
            return true;
        },
        none_do_nothing()
    );
    match_result(
        ProcessTable__push(&self->preferred, process),
        success(code)
        {
            process->thread.preferred_warm = warm;
            process->thread.preferred_ticket = *ticket;
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

void Scheduler__forget_preferred(SchedulerPtr self, ProcessPtr process)
{
    match_optional(
        ProcessTable__find_item(&self->preferred, process),
        some(index)
        {
            match_result(
                ProcessTable__remove(&self->preferred, process),
                success_do_nothing(),
                error(code, message)
                {
                    CPU__halt_all();
                }
            );
        },
        none_do_nothing()
    );
    process->thread.preferred_active = false;
    process->thread.preferred_warm = false;
    process->thread.preferred_dependency_weight = 0;
    process->thread.preferred_ticket = None(RequestTicket);
    process->thread.preferred_eligible_at_usec = 0;
    process->thread.preferred_selected_at_usec = 0;
    process->thread.preferred_dispatched_at_usec = 0;
    process->thread.preferred_trace_pending = false;
}

static bool Scheduler__preferred_valid(SchedulerPtr self, ProcessPtr process)
{
    if (!Process__schedulable(process))
    {
        return false;
    }
    bool exec_ready = Process__has_ready_exec_continuation(process);
    if (!exec_ready && !process->thread.preferred_warm && !Process__shared_wait_ready(process))
    {
        return false;
    }
    match_optional(
        process->thread.preferred_ticket,
        some(ticket)
        {
            match_optional(
                RequestGraph__find(&self->request_graph, ticket.connection, ticket.metadata.generation),
                some(link)
                {
                    return RequestTicket__equals(&link.ticket, &ticket) &&
                           RequestGraph__valid(&self->request_graph, &link);
                },
                none()
                {
                    return false;
                }
            );
        },
        none()
        {
            if (exec_ready)
            {
                return true;
            }
        }
    );
    return Process__shared_wait_ready(process) && Process__has_pending_messages(process);
}

void Scheduler__refresh_preferred(SchedulerPtr self)
{
    RequestGraph__refresh(&self->request_graph, &self->queues);
    for (ArrayIndex index = 0; index < ProcessTable__len(&self->preferred);)
    {
        match_optional(
            ProcessTable__at(&self->preferred, index),
            some(process)
            {
                if (!Scheduler__preferred_valid(self, process))
                {
                    Scheduler__forget_preferred(self, process);
                    continue;
                }
            },
            none_do_nothing()
        );
        index++;
    }
    for_each_fa(
        RequestLinks,
        self->request_graph.links,
        index,
        link,
        {
            if (!RequestGraph__valid(&self->request_graph, &link))
            {
                continue;
            }
            if (!link.replied)
            {
                Scheduler__enqueue_preferred(self, link.responder, false, &Some(RequestTicket, link.ticket));
            }
            else
            {
                RequestLink stage = link;
                for (uint64_t depth = 0; depth < RequestLinks__len(&self->request_graph.links); depth++)
                {
                    Scheduler__enqueue_preferred(self, stage.caller, true, &Some(RequestTicket, stage.ticket));
                    if (stage.ticket.metadata.parent_connection == 0)
                    {
                        break;
                    }
                    match_optional(
                        RequestGraph__find(&self->request_graph, stage.ticket.metadata.parent_connection,
                                           stage.ticket.metadata.parent_generation),
                        some(parent)
                        {
                            stage = parent;
                        },
                        none()
                        {
                            break;
                        }
                    );
                }
            }
        }
    );
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu_id,
        cpu,
        {
            for_each_fa(
                ProcessTable,
                cpu->processes,
                process_index,
                process,
                {
                    if (Process__schedulable(process) && Process__has_ready_exec_continuation(process))
                    {
                        Scheduler__enqueue_preferred(self, process, true, &None(RequestTicket));
                    }
                    else if (Process__schedulable(process) && Process__shared_wait_ready(process) &&
                             Process__has_pending_messages(process))
                    {
                        Scheduler__enqueue_preferred(self, process, false, &None(RequestTicket));
                    }
                }
            );
        }
    );
    for_each_fa(
        ProcessTable,
        self->preferred,
        index,
        process,
        {
            process->thread.preferred_dependency_weight =
                RequestGraph__dependency_weight(&self->request_graph, process);
        }
    );
    self->preferred = ProcessTable__sort_desc(&self->preferred, Process__preferred_priority);
}

static bool CPUScheduler__owns_process(CPUSchedulerPtr * self, void * context)
{
    ProcessPtr process = context;
    match_optional(
        ProcessTable__find_item(&(*self)->processes, process),
        some(index)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static Optional(CPUSchedulerPtr) Scheduler__scheduler_owning_process(SchedulerPtr self, ProcessPtr process)
{
    for_each_hashtable(
        CPUSchedulers,
        self->schedulers,
        bucket_index,
        entry_index,
        cpu_id,
        scheduler,
        {
            if (CPUScheduler__owns_process(&scheduler, process))
            {
                return Some(CPUSchedulerPtr, scheduler);
            }
        }
    );
    return None(CPUSchedulerPtr);
}

Optional(ProcessPtr) Scheduler__take_preferred(SchedulerPtr self, CPUSchedulerPtr cpu)
{
    for_each_fa(
        ProcessTable,
        self->preferred,
        index,
        process,
        {
            if (process->lifecycle.terminating)
            {
                continue;
            }
            if (!Scheduler__preferred_valid(self, process))
            {
                continue;
            }
            match_optional(
                Scheduler__scheduler_owning_process(self, process),
                some(owner)
                {
                    if (!owner->started)
                    {
                        continue;
                    }
                    if (owner != cpu)
                    {
                        if (!process->thread.migration_safe)
                        {
                            continue;
                        }
                        match_optional(
                            owner->switching_out,
                            some(outgoing)
                            {
                                if (outgoing == &process->thread)
                                {
                                    continue;
                                }
                            },
                            none_do_nothing()
                        );
                        if (ProcessTable__len(&cpu->processes) >= MAX_PROCESSES ||
                            ThreadTable__len(&cpu->threads) >= MAX_PROCESSES)
                        {
                            continue;
                        }
                        match_result(
                            ThreadTable__push(&cpu->threads, &process->thread),
                            success_do_nothing(),
                            error(code, message)
                            {
                                __d_printf(SCHEDULER, "Preferred migration queue full: %i\n", code);
                                continue;
                            }
                        );
                        match_result(
                            ProcessTable__push(&cpu->processes, process),
                            success_do_nothing(),
                            error(code, message)
                            {
                                ThreadTable__remove(&cpu->threads, &process->thread);
                                __d_printf(SCHEDULER, "Preferred migration process table full: %i\n", code);
                                continue;
                            }
                        );
                        match_result(
                            ProcessTable__remove(&owner->processes, process),
                            success(index)
                            {
                                ThreadTable__remove(&owner->threads, &process->thread);
                            },
                            error(code, message)
                            {
                                CPU__halt_all();
                                return None(ProcessPtr);
                            }
                        );
                        process->thread.last_tick = cpu->last_tick;
                    }
                    match_result(
                        ProcessTable__remove(&self->preferred, process),
                        success_do_nothing(),
                        error(code, message)
                        {
                            CPU__halt_all();
                            return None(ProcessPtr);
                        }
                    );
                    process->thread.preferred_active = true;
                    process->thread.migration_safe = false;
                    match_optional(
                        process->thread.preferred_ticket,
                        some(ticket)
                        {
                            if (process->thread.preferred_trace_pending)
                            {
                                process->thread.preferred_selected_at_usec = CPU__time_usec();
                            }
                        },
                        none_do_nothing()
                    );
                    return Some(ProcessPtr, process);
                },
                none_do_nothing()
            );
        }
    );
    return None(ProcessPtr);
}

bool Scheduler__ready_exec_continuation_waiting(SchedulerPtr self, ProcessPtr current)
{
    for_each_fa(
        ProcessTable,
        self->preferred,
        index,
        process,
        {
            if (process != current && Scheduler__preferred_valid(self, process) &&
                Process__has_ready_exec_continuation(process))
            {
                return true;
            }
        }
    );
    return false;
}

bool Process__retain_preferred(ProcessPtr self)
{
    if (!self->thread.preferred_active || Process__has_pending_signal(self))
    {
        return false;
    }
    uint64_t elapsed = CPU__time_usec() - self->thread.dispatched_at;
    if (elapsed < SCHEDULER_PREFERRED_BUDGET_USEC &&
        (Process__shared_wait_ready(self) || (self->thread.preferred_warm && elapsed < SCHEDULER_WARM_RESIDENCY_USEC)))
    {
        return true;
    }
    self->thread.preferred_active = false;
    self->thread.preferred_warm = false;
    return false;
}

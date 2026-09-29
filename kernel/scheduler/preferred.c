#include "preferred.h"
#include "shared_wait.h"
#include "../debug.h"

static bool Process__schedulable(ProcessPtr self)
{
    return self->state == PROC_STATE_READY || self->state == PROC_STATE_INTERRUPTED;
}

static bool Process__has_ready_exec_continuation(ProcessPtr self)
{
    return Process__has_ready_parked_operation(self, PARK_WAIT_EXEC_LOAD, &None(OperationKey));
}

static void Process__observe_preferred_ticket(ProcessPtr self, ConstRequestTicketPtr ticket)
{
    bool already_observed = false;
    match_optional(
        self->preferred_ticket,
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
    self->preferred_ticket = Some(RequestTicket, *ticket);
    self->preferred_eligible_at_usec = CPU__time_usec();
    self->preferred_selected_at_usec = 0;
    self->preferred_dispatched_at_usec = 0;
    self->preferred_trace_pending = true;
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
    if (process->state == PROC_STATE_RUNNING)
    {
        process->preferred_active = true;
        process->preferred_warm = warm;
        match_optional(
            process->preferred_ticket,
            some(value)
            {
                if (process->preferred_trace_pending)
                {
                    uint64_t now = CPU__time_usec();
                    process->preferred_selected_at_usec = now;
                    process->preferred_dispatched_at_usec = now;
                    Process__publish_dispatch_trace(process, &value, process->preferred_eligible_at_usec, now, now);
                    process->preferred_trace_pending = false;
                }
            },
            none_do_nothing()
        );
        return true;
    }
    if (process->state == PROC_STATE_SWITCHING_CONTEXT)
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
                process->preferred_warm = true;
                process->preferred_ticket = *ticket;
            }
            return true;
        },
        none_do_nothing()
    );
    match_result(
        ProcessTable__push(&self->preferred, process),
        success(code)
        {
            process->preferred_warm = warm;
            process->preferred_ticket = *ticket;
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
    process->preferred_active = false;
    process->preferred_warm = false;
    process->preferred_ticket = None(RequestTicket);
    process->preferred_eligible_at_usec = 0;
    process->preferred_selected_at_usec = 0;
    process->preferred_dispatched_at_usec = 0;
    process->preferred_trace_pending = false;
}

static bool Scheduler__preferred_valid(SchedulerPtr self, ProcessPtr process)
{
    if (!Process__schedulable(process))
    {
        return false;
    }
    bool exec_ready = Process__has_ready_exec_continuation(process);
    if (!exec_ready && !process->preferred_warm && !Process__shared_wait_ready(process))
    {
        return false;
    }
    match_optional(
        process->preferred_ticket,
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
            match_optional(
                QueueConnections__ptr_at(&self->queues.connections, link.route % QUEUE_LIMIT),
                some(route)
                {
                    if (route->scheduled_generation == link.ticket.metadata.generation &&
                        route->scheduled_reply == link.replied)
                    {
                        continue;
                    }
                    bool queued = true;
                    if (!link.replied)
                    {
                        queued = Scheduler__enqueue_preferred(self, link.responder, false,
                                                              &Some(RequestTicket, link.ticket));
                    }
                    else
                    {
                        queued =
                            Scheduler__enqueue_preferred(self, link.relay, false, &Some(RequestTicket, link.ticket));
                        RequestLink stage = link;
                        for (uint64_t depth = 0; depth < RequestLinks__len(&self->request_graph.links); depth++)
                        {
                            queued = Scheduler__enqueue_preferred(self, stage.caller, true,
                                                                  &Some(RequestTicket, stage.ticket)) &&
                                     queued;
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
                    if (queued)
                    {
                        route->scheduled_generation = link.ticket.metadata.generation;
                        route->scheduled_reply = link.replied;
                    }
                },
                none_do_nothing()
            );
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
                        if (!process->migration_safe)
                        {
                            continue;
                        }
                        match_optional(
                            owner->switching_out,
                            some(outgoing)
                            {
                                if (outgoing == process)
                                {
                                    continue;
                                }
                            },
                            none_do_nothing()
                        );
                        if (ProcessTable__len(&cpu->processes) >= MAX_PROCESSES)
                        {
                            continue;
                        }
                        match_result(
                            ProcessTable__push(&cpu->processes, process),
                            success_do_nothing(),
                            error(code, message)
                            {
                                __d_printf(SCHEDULER, "Preferred migration queue full: %i\n", code);
                                continue;
                            }
                        );
                        match_result(
                            ProcessTable__remove(&owner->processes, process),
                            success_do_nothing(),
                            error(code, message)
                            {
                                CPU__halt_all();
                                return None(ProcessPtr);
                            }
                        );
                        process->last_tick = cpu->last_tick;
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
                    process->preferred_active = true;
                    process->migration_safe = false;
                    match_optional(
                        process->preferred_ticket,
                        some(ticket)
                        {
                            if (process->preferred_trace_pending)
                            {
                                process->preferred_selected_at_usec = CPU__time_usec();
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
    if (!self->preferred_active || Process__has_pending_signal(self))
    {
        return false;
    }
    uint64_t elapsed = CPU__time_usec() - self->dispatched_at;
    if (elapsed < SCHEDULER_PREFERRED_BUDGET_USEC &&
        (Process__shared_wait_ready(self) || (self->preferred_warm && elapsed < SCHEDULER_WARM_RESIDENCY_USEC)))
    {
        return true;
    }
    self->preferred_active = false;
    self->preferred_warm = false;
    return false;
}

#include "io_latency.h"

#define IO_LATENCY_PUBLISHED 1U
#define IO_LATENCY_REQUEST_ELIGIBLE 2U
#define IO_LATENCY_REQUEST_SELECTED 4U
#define IO_LATENCY_REQUEST_DISPATCHED 8U
#define IO_LATENCY_RECEIVED 16U
#define IO_LATENCY_REPLIED 32U
#define IO_LATENCY_RETURN_ELIGIBLE 64U
#define IO_LATENCY_RETURN_SELECTED 128U
#define IO_LATENCY_RETURN_DISPATCHED 256U
#define IO_LATENCY_COMPLETED 512U
#define IO_LATENCY_CHANNEL_COMPLETE                                                                                    \
    (IO_LATENCY_PUBLISHED | IO_LATENCY_RECEIVED | IO_LATENCY_REPLIED | IO_LATENCY_COMPLETED)
#define IO_LATENCY_REQUEST_SCHEDULER                                                                                   \
    (IO_LATENCY_REQUEST_ELIGIBLE | IO_LATENCY_REQUEST_SELECTED | IO_LATENCY_REQUEST_DISPATCHED)
#define IO_LATENCY_RETURN_SCHEDULER                                                                                    \
    (IO_LATENCY_RETURN_ELIGIBLE | IO_LATENCY_RETURN_SELECTED | IO_LATENCY_RETURN_DISPATCHED)

FixedArrayBodyDefine(IOLatencySpans);
OptionalDef(IOLatencySpanPtr);

bool IOLatencySpan__equals(void * first, void * second)
{
    IOLatencySpan * left = first;
    IOLatencySpan * right = second;
    return RequestTicket__equals(&left->ticket, &right->ticket);
}

static IOLatencySpan IOLatencySpan__new(ConstRequestTicketPtr ticket)
{
    return (IOLatencySpan){.ticket = *ticket};
}

IOLatencyTracker IOLatencyTracker__new(void)
{
    return (IOLatencyTracker){.spans = IOLatencySpans__new()};
}

Optional(BString) IOLatencyTracker__hop(uint64_t server_pid, ConstIOTraceEventPtr event)
{
    if (event->kind != IO_TRACE_SERVER_RECEIVE || event->peer_pid == 0 || event->peer_timestamp_usec == 0 ||
        event->peer_timestamp_usec > event->timestamp_usec)
    {
        return None(BString);
    }
    return Some(BString, BString__sprintf(&bstr("client=%llu server=%llu connection=%llu generation=%llu origin=%llu "
                                                "parent_connection=%llu parent_generation=%llu published_at_usec=%llu "
                                                "consumed_at_usec=%llu queue_usec=%llu"),
                                          event->peer_pid, server_pid, event->ticket.connection,
                                          event->ticket.metadata.generation, event->ticket.metadata.origin_pid,
                                          event->ticket.metadata.parent_connection,
                                          event->ticket.metadata.parent_generation, event->peer_timestamp_usec,
                                          event->timestamp_usec, event->timestamp_usec - event->peer_timestamp_usec));
}

static void IOLatencySpan__observe(IOLatencySpan * self, uint64_t pid, ConstIOTraceEventPtr event)
{
    switch (event->kind)
    {
    case IO_TRACE_CLIENT_PUBLISH:
        self->client_pid = pid;
        self->published_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_PUBLISHED;
        break;
    case IO_TRACE_SERVER_RECEIVE:
        self->server_pid = pid;
        self->received_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_RECEIVED;
        break;
    case IO_TRACE_SERVER_REPLY:
        self->server_pid = pid;
        self->replied_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_REPLIED;
        break;
    case IO_TRACE_CLIENT_COMPLETE:
        self->client_pid = pid;
        self->completed_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_COMPLETED;
        break;
    case IO_TRACE_REQUEST_KERNEL_ELIGIBLE:
        self->request_eligible_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_REQUEST_ELIGIBLE;
        break;
    case IO_TRACE_REQUEST_KERNEL_SELECTED:
        self->request_selected_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_REQUEST_SELECTED;
        break;
    case IO_TRACE_REQUEST_KERNEL_DISPATCHED:
        self->request_dispatched_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_REQUEST_DISPATCHED;
        break;
    case IO_TRACE_RETURN_KERNEL_ELIGIBLE:
        self->return_eligible_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_RETURN_ELIGIBLE;
        break;
    case IO_TRACE_RETURN_KERNEL_SELECTED:
        self->return_selected_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_RETURN_SELECTED;
        break;
    case IO_TRACE_RETURN_KERNEL_DISPATCHED:
        self->return_dispatched_at_usec = event->timestamp_usec;
        self->observations |= IO_LATENCY_RETURN_DISPATCHED;
        break;
    }
}

static Optional(BString) IOLatencySpan__summary(IOLatencySpan * self)
{
    bool request_scheduler_observed =
        (self->observations & IO_LATENCY_REQUEST_SCHEDULER) == IO_LATENCY_REQUEST_SCHEDULER;
    bool return_scheduler_observed = (self->observations & IO_LATENCY_RETURN_SCHEDULER) == IO_LATENCY_RETURN_SCHEDULER;
    if ((self->observations & IO_LATENCY_CHANNEL_COMPLETE) != IO_LATENCY_CHANNEL_COMPLETE ||
        self->published_at_usec > self->received_at_usec || self->received_at_usec > self->replied_at_usec ||
        self->replied_at_usec > self->completed_at_usec ||
        (request_scheduler_observed && (self->published_at_usec > self->request_eligible_at_usec ||
                                        self->request_eligible_at_usec > self->request_selected_at_usec ||
                                        self->request_selected_at_usec > self->request_dispatched_at_usec ||
                                        self->request_dispatched_at_usec > self->received_at_usec)) ||
        (return_scheduler_observed && (self->replied_at_usec > self->return_eligible_at_usec ||
                                       self->return_eligible_at_usec > self->return_selected_at_usec ||
                                       self->return_selected_at_usec > self->return_dispatched_at_usec ||
                                       self->return_dispatched_at_usec > self->completed_at_usec)))
    {
        return None(BString);
    }
    uint64_t queue_usec = self->received_at_usec - self->published_at_usec;
    uint64_t request_observe_usec =
        request_scheduler_observed ? self->request_eligible_at_usec - self->published_at_usec : 0;
    uint64_t request_runnable_usec =
        request_scheduler_observed ? self->request_selected_at_usec - self->request_eligible_at_usec : 0;
    uint64_t request_dispatch_usec =
        request_scheduler_observed ? self->request_dispatched_at_usec - self->request_selected_at_usec : 0;
    uint64_t request_userspace_usec =
        request_scheduler_observed ? self->received_at_usec - self->request_dispatched_at_usec : 0;
    uint64_t service_usec = self->replied_at_usec - self->received_at_usec;
    uint64_t return_usec = self->completed_at_usec - self->replied_at_usec;
    uint64_t return_observe_usec =
        return_scheduler_observed ? self->return_eligible_at_usec - self->replied_at_usec : 0;
    uint64_t return_runnable_usec =
        return_scheduler_observed ? self->return_selected_at_usec - self->return_eligible_at_usec : 0;
    uint64_t return_dispatch_usec =
        return_scheduler_observed ? self->return_dispatched_at_usec - self->return_selected_at_usec : 0;
    uint64_t return_userspace_usec =
        return_scheduler_observed ? self->completed_at_usec - self->return_dispatched_at_usec : 0;
    uint64_t total_usec = self->completed_at_usec - self->published_at_usec;
    return Some(BString,
                BString__sprintf(&bstr("client=%llu server=%llu connection=%llu generation=%llu origin=%llu "
                                       "parent_connection=%llu parent_generation=%llu queue_usec=%llu "
                                       "published_at_usec=%llu received_at_usec=%llu replied_at_usec=%llu "
                                       "completed_at_usec=%llu "
                                       "request_scheduler_observed=%llu request_observe_usec=%llu "
                                       "request_runnable_usec=%llu "
                                       "request_dispatch_usec=%llu request_userspace_usec=%llu service_usec=%llu "
                                       "return_usec=%llu return_scheduler_observed=%llu return_observe_usec=%llu "
                                       "return_runnable_usec=%llu "
                                       "return_dispatch_usec=%llu return_userspace_usec=%llu total_usec=%llu"),
                                 self->client_pid, self->server_pid, self->ticket.connection,
                                 self->ticket.metadata.generation, self->ticket.metadata.origin_pid,
                                 self->ticket.metadata.parent_connection, self->ticket.metadata.parent_generation,
                                 queue_usec, self->published_at_usec, self->received_at_usec, self->replied_at_usec,
                                 self->completed_at_usec, request_scheduler_observed, request_observe_usec,
                                 request_runnable_usec, request_dispatch_usec, request_userspace_usec, service_usec,
                                 return_usec, return_scheduler_observed, return_observe_usec, return_runnable_usec,
                                 return_dispatch_usec, return_userspace_usec, total_usec));
}

static Optional(IOLatencySpanPtr) IOLatencyTracker__span(IOLatencyTrackerPtr self, ConstRequestTicketPtr ticket)
{
    IOLatencySpan key = IOLatencySpan__new(ticket);
    match_optional(
        IOLatencySpans__find_item(&self->spans, key),
        some(index)
        {
            match_optional(
                IOLatencySpans__ptr_at(&self->spans, index),
                some(span)
                {
                    return Some(IOLatencySpanPtr, span);
                },
                none()
                {
                    return None(IOLatencySpanPtr);
                }
            );
        },
        none()
        {
            if (IOLatencySpans__len(&self->spans) == IO_LATENCY_SPAN_LIMIT)
            {
                IOLatencySpans__pop_front(&self->spans);
            }
            match_result(
                IOLatencySpans__push(&self->spans, key),
                success(code)
                {
                    match_optional(
                        IOLatencySpans__ptr_at(&self->spans, code),
                        some(span)
                        {
                            return Some(IOLatencySpanPtr, span);
                        },
                        none()
                        {
                            return None(IOLatencySpanPtr);
                        }
                    );
                },
                error(code, message)
                {
                    return None(IOLatencySpanPtr);
                }
            );
        }
    );
    return None(IOLatencySpanPtr);
}

Optional(BString) IOLatencyTracker__record(IOLatencyTrackerPtr self, uint64_t pid, ConstIOTraceEventPtr event)
{
    match_optional(
        IOLatencyTracker__span(self, &event->ticket),
        some(span)
        {
            IOLatencySpan__observe(span, pid, event);
            Optional(BString) summary = IOLatencySpan__summary(span);
            match_optional(summary,
                some(value)
                {
                    IOLatencySpans__remove(&self->spans, *span);
                },
                none_do_nothing()
            );
            return summary;
        },
        none()
        {
            return None(BString);
        }
    );
    return None(BString);
}

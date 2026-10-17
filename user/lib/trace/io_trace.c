#include "io_trace.h"

#include "buffer/client.h"
#include "channel/channel.h"
#include "ipc/trace_protocol.h"
#include "services/service_directory.h"
#include "sys/queue_wait.h"
#include "sys/system_info.h"

static bool enabled = true;
static bool registering;
static Optional(IOTraceRingPtr) ring = None(IOTraceRingPtr);

static Optional(IOTraceRingPtr) IOTrace__open_ring(void)
{
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(HUND_VERFOLGEN_SERVICE)),
        success(channel)
        {
            TraceRequest request = TraceRequest__register_io();
            TraceReply reply;
            match_result(
                Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply,
                                sizeof(reply)),
                success(size)
                {
                    if (size == sizeof(reply) && reply.status == TRACE_OK &&
                        BufferClient__validate(&reply.buffer, Channel__server_pid(&channel), BUFFER_WRITE))
                    {
                        IOTraceRingPtr opened = (IOTraceRingPtr)reply.buffer.address;
                        IOTraceRing__initialize(opened);
                        Channel__close(&channel);
                        return Some(IOTraceRingPtr, opened);
                    }
                    Channel__close(&channel);
                    return None(IOTraceRingPtr);
                },
                error(code, message)
                {
                    Channel__close(&channel);
                    return None(IOTraceRingPtr);
                }
            );
        },
        error(code, message)
        {
            return None(IOTraceRingPtr);
        }
    );
    return None(IOTraceRingPtr);
}

static bool IOTrace__register(void)
{
    registering = true;
    ring = IOTrace__open_ring();
    registering = false;
    match_optional(ring,
        some(target)
        {
            return true;
        },
        none()
        {
            return false;
        }
    );
    return false;
}

static TraceStatus IOTrace__submit_drain(void)
{
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(HUND_VERFOLGEN_SERVICE)),
        success(channel)
        {
            TraceRequest request = TraceRequest__drain_io();
            TraceReply reply;
            match_result(
                Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply,
                                sizeof(reply)),
                success(size)
                {
                    Channel__close(&channel);
                    return size == sizeof(reply) ? reply.status : TRACE_INVALID_REQUEST;
                },
                error(code, message)
                {
                    Channel__close(&channel);
                    return TRACE_UNAVAILABLE;
                }
            );
        },
        error(code, message)
        {
            return TRACE_UNAVAILABLE;
        }
    );
    return TRACE_UNAVAILABLE;
}

TraceStatus IOTrace__drain(void)
{
    if (!enabled || registering || is_nothing(ring))
    {
        return TRACE_OK;
    }

    registering = true;
    TraceStatus status = IOTrace__submit_drain();
    registering = false;
    return status;
}

void IOTrace__set_enabled(bool value)
{
    enabled = value;
}

void IOTrace__reset_after_fork(void)
{
    registering = false;
    ring = None(IOTraceRingPtr);
}

TraceStatus IOTrace__prepare(void)
{
    if (!enabled || is_something(ring))
    {
        return TRACE_OK;
    }
    if (registering)
    {
        return TRACE_UNAVAILABLE;
    }
    match_result(
        SystemSettings__load(),
        success(settings)
        {
            if (!settings.io_latency_tracing_enabled)
            {
                enabled = false;
                return TRACE_OK;
            }
        },
        error(code, message)
        {
            return TRACE_UNAVAILABLE;
        }
    );
    return IOTrace__register() ? TRACE_OK : TRACE_UNAVAILABLE;
}

static void IOTrace__record_dispatch(IOTraceRingPtr target, IOTraceEventKind kind, ConstRequestTicketPtr ticket)
{
    IOTraceEventKind eligible_kind;
    IOTraceEventKind selected_kind;
    IOTraceEventKind dispatched_kind;
    if (kind == IO_TRACE_SERVER_RECEIVE)
    {
        eligible_kind = IO_TRACE_REQUEST_KERNEL_ELIGIBLE;
        selected_kind = IO_TRACE_REQUEST_KERNEL_SELECTED;
        dispatched_kind = IO_TRACE_REQUEST_KERNEL_DISPATCHED;
    }
    else if (kind == IO_TRACE_CLIENT_COMPLETE)
    {
        eligible_kind = IO_TRACE_RETURN_KERNEL_ELIGIBLE;
        selected_kind = IO_TRACE_RETURN_KERNEL_SELECTED;
        dispatched_kind = IO_TRACE_RETURN_KERNEL_DISPATCHED;
    }
    else
    {
        return;
    }
    match_optional(
        QueueWait__take_dispatch_trace(ticket),
        some(trace)
        {
            if (trace.eligible_at_usec > trace.selected_at_usec || trace.selected_at_usec > trace.dispatched_at_usec)
            {
                return;
            }
            IOTraceEvent eligible = IOTraceEvent__new(eligible_kind, ticket, trace.eligible_at_usec);
            IOTraceEvent selected = IOTraceEvent__new(selected_kind, ticket, trace.selected_at_usec);
            IOTraceEvent dispatched = IOTraceEvent__new(dispatched_kind, ticket, trace.dispatched_at_usec);
            IOTraceRing__publish(target, &eligible);
            IOTraceRing__publish(target, &selected);
            IOTraceRing__publish(target, &dispatched);
        },
        none_do_nothing()
    );
}

void IOTrace__record(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec)
{
    if (!enabled || registering || !RequestTicket__valid(ticket))
    {
        return;
    }
    match_optional(
        ring,
        some(target)
        {
            IOTrace__record_dispatch(target, kind, ticket);
            IOTraceEvent event = IOTraceEvent__new(kind, ticket, timestamp_usec);
            IOTraceRing__publish(target, &event);
        },
        none_do_nothing()
    );
}

void IOTrace__record_with_peer(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec,
                               uint64_t peer_pid, uint64_t peer_timestamp_usec)
{
    if (!enabled || registering || !RequestTicket__valid(ticket))
    {
        return;
    }
    match_optional(
        ring,
        some(target)
        {
            IOTrace__record_dispatch(target, kind, ticket);
            IOTraceEvent event =
                IOTraceEvent__new_with_peer(kind, ticket, timestamp_usec, peer_pid, peer_timestamp_usec);
            IOTraceRing__publish(target, &event);
        },
        none_do_nothing()
    );
}

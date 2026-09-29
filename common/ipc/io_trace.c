#include "io_trace.h"

IOTraceEvent IOTraceEvent__new(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec)
{
    return (IOTraceEvent){.ticket = *ticket, .timestamp_usec = timestamp_usec, .kind = kind};
}

IOTraceEvent IOTraceEvent__new_with_peer(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec,
                                         uint64_t peer_pid, uint64_t peer_timestamp_usec)
{
    return (IOTraceEvent){
        .ticket = *ticket,
        .timestamp_usec = timestamp_usec,
        .peer_pid = peer_pid,
        .peer_timestamp_usec = peer_timestamp_usec,
        .kind = kind,
    };
}

void IOTraceRing__initialize(IOTraceRingPtr self)
{
    __builtin_memset(self, 0, sizeof(*self));
    __atomic_store_n(&self->magic, IO_TRACE_RING_MAGIC, __ATOMIC_RELEASE);
}

bool IOTraceRing__ready(IOTraceRingPtr self)
{
    return __atomic_load_n(&self->magic, __ATOMIC_ACQUIRE) == IO_TRACE_RING_MAGIC;
}

void IOTraceRing__publish(IOTraceRingPtr self, ConstIOTraceEventPtr event)
{
    uint64_t produced = __atomic_load_n(&self->produced, __ATOMIC_RELAXED);
    IOTraceSlot * slot = &self->slots[produced % IO_TRACE_RING_CAPACITY];
    __atomic_store_n(&slot->sequence, 0, __ATOMIC_RELEASE);
    slot->event = *event;
    __atomic_store_n(&slot->sequence, produced + 1, __ATOMIC_RELEASE);
    __atomic_store_n(&self->produced, produced + 1, __ATOMIC_RELEASE);
}

uint64_t IOTraceRing__produced(IOTraceRingPtr self)
{
    return __atomic_load_n(&self->produced, __ATOMIC_ACQUIRE);
}

uint64_t IOTraceRing__oldest(IOTraceRingPtr self, uint64_t produced)
{
    return produced > IO_TRACE_RING_CAPACITY ? produced - IO_TRACE_RING_CAPACITY : 0;
}

Optional(IOTraceEvent) IOTraceRing__read(IOTraceRingPtr self, uint64_t cursor)
{
    IOTraceSlot * slot = &self->slots[cursor % IO_TRACE_RING_CAPACITY];
    uint64_t expected = cursor + 1;
    uint64_t before = __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE);
    if (before != expected)
    {
        return None(IOTraceEvent);
    }
    IOTraceEvent event = slot->event;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE) == before ? Some(IOTraceEvent, event)
                                                                        : None(IOTraceEvent);
}

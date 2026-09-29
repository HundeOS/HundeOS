#ifndef HUND_IO_TRACE_H
#define HUND_IO_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "request_ticket.h"
#include "../types/optional.h"

#define IO_TRACE_RING_MAGIC 0x48554e4454524143ULL
#define IO_TRACE_RING_CAPACITY 4096U
#define IO_TRACE_LOG_PATH "/tmp/io-trace.log"

typedef enum IOTraceEventKind
{
    IO_TRACE_CLIENT_PUBLISH = 1,
    IO_TRACE_SERVER_RECEIVE,
    IO_TRACE_SERVER_REPLY,
    IO_TRACE_CLIENT_COMPLETE,
    IO_TRACE_REQUEST_KERNEL_ELIGIBLE,
    IO_TRACE_REQUEST_KERNEL_SELECTED,
    IO_TRACE_REQUEST_KERNEL_DISPATCHED,
    IO_TRACE_RETURN_KERNEL_ELIGIBLE,
    IO_TRACE_RETURN_KERNEL_SELECTED,
    IO_TRACE_RETURN_KERNEL_DISPATCHED,
} IOTraceEventKind;

typedef struct IOTraceEvent
{
    RequestTicket ticket;
    uint64_t timestamp_usec;
    uint64_t peer_pid;
    uint64_t peer_timestamp_usec;
    IOTraceEventKind kind;
} IOTraceEvent;

typedef IOTraceEvent * IOTraceEventPtr;
typedef const IOTraceEvent * ConstIOTraceEventPtr;
OptionalDef(IOTraceEvent);

typedef struct IOTraceSlot
{
    uint64_t sequence;
    IOTraceEvent event;
} IOTraceSlot;

typedef struct IOTraceRing
{
    uint64_t magic;
    uint64_t produced;
    IOTraceSlot slots[IO_TRACE_RING_CAPACITY];
} IOTraceRing;

typedef IOTraceRing * IOTraceRingPtr;
OptionalDef(IOTraceRingPtr);

IOTraceEvent IOTraceEvent__new(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec);
IOTraceEvent IOTraceEvent__new_with_peer(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec,
                                         uint64_t peer_pid, uint64_t peer_timestamp_usec);
void IOTraceRing__initialize(IOTraceRingPtr self);
bool IOTraceRing__ready(IOTraceRingPtr self);
void IOTraceRing__publish(IOTraceRingPtr self, ConstIOTraceEventPtr event);
uint64_t IOTraceRing__produced(IOTraceRingPtr self);
uint64_t IOTraceRing__oldest(IOTraceRingPtr self, uint64_t produced);
Optional(IOTraceEvent) IOTraceRing__read(IOTraceRingPtr self, uint64_t cursor);

#endif

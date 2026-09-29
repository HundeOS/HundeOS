#ifndef HUND_VERFOLGEN_IO_LATENCY_H
#define HUND_VERFOLGEN_IO_LATENCY_H

#include "ipc/io_trace.h"
#include "types/bstring.h"
#include "types/fixed_array.h"

#define IO_LATENCY_SPAN_LIMIT 1024U

typedef struct IOLatencySpan
{
    RequestTicket ticket;
    uint64_t client_pid;
    uint64_t server_pid;
    uint64_t published_at_usec;
    uint64_t request_eligible_at_usec;
    uint64_t request_selected_at_usec;
    uint64_t request_dispatched_at_usec;
    uint64_t received_at_usec;
    uint64_t replied_at_usec;
    uint64_t return_eligible_at_usec;
    uint64_t return_selected_at_usec;
    uint64_t return_dispatched_at_usec;
    uint64_t completed_at_usec;
    uint16_t observations;
} IOLatencySpan;

typedef IOLatencySpan * IOLatencySpanPtr;
OptionalDef(IOLatencySpan);
bool IOLatencySpan__equals(void * first, void * second);
FixedArrayDefine(IOLatencySpan, IO_LATENCY_SPAN_LIMIT, IOLatencySpans, IOLatencySpan__equals);

typedef struct IOLatencyTracker
{
    IOLatencySpans spans;
} IOLatencyTracker;

typedef IOLatencyTracker * IOLatencyTrackerPtr;

IOLatencyTracker IOLatencyTracker__new(void);
Optional(BString) IOLatencyTracker__hop(uint64_t server_pid, ConstIOTraceEventPtr event);
Optional(BString) IOLatencyTracker__record(IOLatencyTrackerPtr self, uint64_t pid, ConstIOTraceEventPtr event);

#endif

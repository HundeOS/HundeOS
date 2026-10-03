#ifndef HUND_USER_IO_TRACE_H
#define HUND_USER_IO_TRACE_H

#include "ipc/trace_protocol.h"

void IOTrace__set_enabled(bool enabled);
void IOTrace__reset_after_fork(void);
TraceStatus IOTrace__prepare(void);
void IOTrace__record(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec);
void IOTrace__record_with_peer(IOTraceEventKind kind, ConstRequestTicketPtr ticket, uint64_t timestamp_usec,
                               uint64_t peer_pid, uint64_t peer_timestamp_usec);
TraceStatus IOTrace__drain(void);

#endif

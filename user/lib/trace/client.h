#ifndef HUND_USER_TRACE_CLIENT_H
#define HUND_USER_TRACE_CLIENT_H

#include "ipc/trace_protocol.h"

TraceStatus DriverTrace__write(ConstBStringPtr tag, ConstBStringPtr message);
TraceStatus DriverTrace__writef(const char * tag, const char * format, ...);
TraceStatus DriverTrace__storage_ready(void);

#endif

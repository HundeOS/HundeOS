#include "client.h"

#include <stdarg.h>

#include "sys/syslog.h"

static TraceStatus DriverTrace__status(SyslogResult result)
{
    return result == SYSLOG_OK ? TRACE_OK : TRACE_UNAVAILABLE;
}

TraceStatus DriverTrace__write(ConstBStringPtr tag, ConstBStringPtr message)
{
    return DriverTrace__status(kernel_syslog_tag(tag, message));
}

static TraceStatus DriverTrace__writev(const char * tag, const char * format, va_list args)
{
    BString format_string = BString__new(format);
    BString message = BString__vsprintf(&format_string, args);
    BString tag_string = BString__new(tag);
    return DriverTrace__status(kernel_syslog_tag(&tag_string, &message));
}

TraceStatus DriverTrace__writef(const char * tag, const char * format, ...)
{
    va_list args;
    va_start(args, format);
    TraceStatus status = DriverTrace__writev(tag, format, args);
    va_end(args);
    return status;
}

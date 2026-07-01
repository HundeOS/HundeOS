#include "syslog.h"

#include <stdarg.h>

#include "syscall.h"
#include "syscalls/calls.h"

int sprintf_impl(char * str, const char * format, va_list args);

SyslogResult kernel_syslog(const char * message)
{
    return (SyslogResult) syscall(SYSCALL_SYSLOG, (Anything) message, 0, 0, 0, 0);
}

SyslogResult kernel_syslog_tag(const char * tag, const char * message)
{
    return (SyslogResult) syscall(SYSCALL_SYSLOG, (Anything) message, (Anything) tag, 0, 0, 0);
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    char buffer[SYSLOG_MAX_MESSAGE];
    va_list args;
    va_start(args, format);
    sprintf_impl(buffer, format, args);
    va_end(args);
    return kernel_syslog(buffer);
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    char buffer[SYSLOG_MAX_MESSAGE];
    va_list args;
    va_start(args, format);
    sprintf_impl(buffer, format, args);
    va_end(args);
    return kernel_syslog_tag(tag, buffer);
}

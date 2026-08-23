#include "syslog.h"

#include <stdarg.h>

#include "../../common/types/bstring.h"
#include "syscall.h"
#include "syscalls/calls.h"

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
    BString format_string = BString__new(format);
    va_list args;
    va_start(args, format);
    BString message = BString__vsprintf(&format_string, args);
    va_end(args);
    return kernel_syslog(BString__cstr(&message));
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    BString format_string = BString__new(format);
    va_list args;
    va_start(args, format);
    BString message = BString__vsprintf(&format_string, args);
    va_end(args);
    return kernel_syslog_tag(tag, BString__cstr(&message));
}

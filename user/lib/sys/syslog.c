#include "syslog.h"

#include <stdarg.h>

#include "../../common/types/bstring.h"
#include "syscall.h"
#include "syscalls/calls.h"

SyslogResult kernel_syslog(ConstBStringPtr message)
{
    SyslogRequest request = SyslogRequest__new(message, None(BString));
    return (SyslogResult) syscall(SYSCALL_SYSLOG, (Anything) &request, 0, 0, 0, 0);
}

SyslogResult kernel_syslog_tag(ConstBStringPtr tag, ConstBStringPtr message)
{
    SyslogRequest request = SyslogRequest__new(message, Some(BString, *tag));
    return (SyslogResult) syscall(SYSCALL_SYSLOG, (Anything) &request, 0, 0, 0, 0);
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    BString format_string = BString__new(format);
    va_list args;
    va_start(args, format);
    BString message = BString__vsprintf(&format_string, args);
    va_end(args);
    return kernel_syslog(&message);
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    BString format_string = BString__new(format);
    va_list args;
    va_start(args, format);
    BString message = BString__vsprintf(&format_string, args);
    va_end(args);
    BString tag_string = BString__new(tag);
    return kernel_syslog_tag(&tag_string, &message);
}

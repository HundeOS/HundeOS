#include "syslog_handler.h"

#include "../debug.h"
#include "syscalls/syslog.h"

static BString invalid_syslog_message = bstr("Invalid syslog message");

static void Syslog__write(ConstBStringPtr tag, OptionalPtr(ProcessPtr) proc, ConstBStringPtr message)
{
    if (!__d_should_log(SYSLOG))
    {
        return;
    }

    match_optional(
        *proc,
        some(process)
        {
            k_printf("cpu=%i - %s - ", CPU__id(), cstr(*tag));
            k_printf("pid=%i name=%s: %s", process->pid, cstr(process->name), cstr(*message));
            return;
        },
        none()
        {
            k_printf("cpu=%i - %s - ", CPU__id(), cstr(*tag));
            k_printf("pid=none: %s", cstr(*message));
            return;
        }
    );
}

Result(SystemCallResult) Syslog__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                         Anything arg3, Anything arg4, Anything arg5)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_syslog_message, SYSLOG_INVALID_ARGUMENT);
    }

    SyslogRequestPtr request = (SyslogRequestPtr)arg1;
    BString tag = bstr("SYSLOG");
    match_optional(request->tag,
        some(copied_tag)
        {
            tag = copied_tag;
        },
        none_do_nothing()
    );

    Syslog__write(&tag, proc, &request->message);
    return Success(SystemCallResult, SYSLOG_OK);
}

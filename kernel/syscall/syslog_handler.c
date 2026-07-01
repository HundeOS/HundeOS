#include "syslog_handler.h"

#include "../debug.h"
#include "syscalls/syslog.h"

static KString invalid_syslog_message = KSTRING_NEW("Invalid syslog message");

static Result(KString) Syslog__copy_string(Anything string_address, uint64_t max_len)
{
    if (string_address == 0)
    {
        return Error(KString, invalid_syslog_message, SYSLOG_INVALID_ARGUMENT);
    }

    char * source = (char *) string_address;
    KString message = EMPTY_KSTRING;
    if (max_len > message.max - 1)
    {
        max_len = message.max - 1;
    }

    uint64_t position = 0;
    while (position < max_len && source[position] != '\0')
    {
        message.data[position] = source[position];
        position++;
    }

    message.data[position] = '\0';
    message.len = position;
    return Success(KString, message);
}

static Result(KString) Syslog__copy_message(Anything message_address)
{
    return Syslog__copy_string(message_address, SYSLOG_MAX_MESSAGE - 1);
}

static Result(KString) Syslog__copy_tag(Anything tag_address)
{
    return Syslog__copy_string(tag_address, SYSLOG_MAX_TAG - 1);
}

static void Syslog__write(KString tag, Optional(ProcessPtr) proc, KString message)
{
    if (!__d_should_log(SYSLOG))
    {
        return;
    }

    Match(ProcessPtr, proc)
        IsSome(process,
        {
            k_printf("cpu=%i - %s - ", CPU__id(), cstr(tag));
            k_printf("pid=%i name=%s: %s", process->pid, cstr(process->name), cstr(message));
            return;
        })
        IsNone(
        {
            k_printf("cpu=%i - %s - ", CPU__id(), cstr(tag));
            k_printf("pid=none: %s", cstr(message));
            return;
        })
    End
}

Result(SystemCallResult) Syslog__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
)
{
    ResultMatch(KString, Syslog__copy_message(arg1))
        IsSuccess(message,
        {
            KString tag = KSTRING_NEW("SYSLOG");
            if (arg2 != 0)
            {
                ResultMatch(KString, Syslog__copy_tag(arg2))
                    IsSuccess(copied_tag, tag = copied_tag)
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            }

            Syslog__write(tag, proc, message);
            return Success(SystemCallResult, SYSLOG_OK);
        })
        IsError(code, message, return Error(SystemCallResult, message, code))
    End

    return Error(SystemCallResult, invalid_syslog_message, SYSLOG_INVALID_ARGUMENT);
}

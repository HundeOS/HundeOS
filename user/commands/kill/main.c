#include "../../user.h"
#include "../../sys/process.h"

static const BString KILL__SIGNAL_PREFIX = bstr("SIG");
static const BString KILL__TERM_SIGNAL = bstr("TERM");
static const BString KILL__KILL_SIGNAL = bstr("KILL");
static const BString KILL__INT_SIGNAL = bstr("INT");

static bool Kill__matches_signal_name(ConstBStringPtr value, ConstBStringPtr expected)
{
    BString trimmed = *value;
    if (BString__starts_with(&trimmed, &KILL__SIGNAL_PREFIX))
    {
        trimmed = BString__slice(&trimmed, BString__length(&KILL__SIGNAL_PREFIX), BString__length(&trimmed));
    }

    BString uppercase = BString__uppercase(&trimmed);
    return BString__equals(&uppercase, expected);
}

static bool Kill__parse_signal_spec(ConstBStringPtr value, ProcessSignal * signal)
{
    match_optional(uint64_t, BString__parse_uint64(value),
        some(numeric_signal)
        {
            *signal = (ProcessSignal) numeric_signal;
            return true;
        },
        none_do_nothing()
    );

    if (Kill__matches_signal_name(value, &KILL__TERM_SIGNAL))
    {
        *signal = PROCESS_SIGNAL_TERMINATE;
        return true;
    }

    if (Kill__matches_signal_name(value, &KILL__KILL_SIGNAL))
    {
        *signal = PROCESS_SIGNAL_KILL;
        return true;
    }

    if (Kill__matches_signal_name(value, &KILL__INT_SIGNAL))
    {
        *signal = PROCESS_SIGNAL_INTERRUPT;
        return true;
    }

    return false;
}

static const char * Kill__signal_result_string(ProcessSignalResult result)
{
    switch (result)
    {
        case PROCESS_SIGNAL_OK:
            return "PROCESS_SIGNAL_OK";
        case PROCESS_SIGNAL_INVALID_ARGUMENT:
            return "PROCESS_SIGNAL_INVALID_ARGUMENT";
        case PROCESS_SIGNAL_PROCESS_NOT_FOUND:
            return "PROCESS_SIGNAL_PROCESS_NOT_FOUND";
        case PROCESS_SIGNAL_UNSUPPORTED:
            return "PROCESS_SIGNAL_UNSUPPORTED";
        default:
            return "PROCESS_SIGNAL_UNKNOWN";
    }
}

int main(int argc, char ** argv)
{
    ProcessSignal signal = PROCESS_SIGNAL_TERMINATE;
    ConstBStringPtr pid_arg = NULL; // hundelint: ignore-no-null
    BString pid_storage = empty_bstr();

    if (argc == 2)
    {
        pid_storage = BString__new(argv[1]);
        pid_arg = &pid_storage;
    }
    else if (argc == 3 && argv[1][0] == '-')
    {
        BString signal_arg = BString__new(argv[1] + 1);
        if (!Kill__parse_signal_spec(&signal_arg, &signal))
        {
            IO__print(&bstr("kill: usage kill [-signal] <pid>\n"));
            return 1;
        }
        pid_storage = BString__new(argv[2]);
        pid_arg = &pid_storage;
    }
    else
    {
        IO__print(&bstr("kill: usage kill [-signal] <pid>\n"));
        return 1;
    }

    match_optional(uint64_t, BString__parse_uint64(pid_arg),
        some(pid)
        {
            ProcessSignalResult result = kill_process(pid, signal);
            if (result != PROCESS_SIGNAL_OK)
            {
                IO__printf(&bstr("kill: error %s (%i)\n"), Kill__signal_result_string(result), (int) result);
                return 1;
            }

            return 0;
        },
        none()
        {
            IO__print(&bstr("kill: usage kill [-signal] <pid>\n"));
            return 1;
        }
    );

    return 1;
}

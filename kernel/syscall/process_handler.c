#include "process_handler.h"

#include "syscalls/process.h"

static KString invalid_process_list_request = KSTRING_NEW("Invalid process list request");

static void ProcessInfo__copy_kstring(char * target, uint64_t target_size, KStringPtr source)
{
    uint64_t position = 0;
    while (position + 1 < target_size && position < source->len)
    {
        target[position] = source->data[position];
        position++;
    }
    target[position] = '\0';
}

static KString ProcessClass__snapshot_name(ProcessClass process_class)
{
    switch (process_class)
    {
        case PROCESS_CLASS_REALTIME_SERVICE:
            return KSTRING_NEW("rt-service");
        case PROCESS_CLASS_SERVICE:
            return KSTRING_NEW("service");
        case PROCESS_CLASS_NORMAL:
        default:
            return KSTRING_NEW("normal");
    }
}

static ProcessInfo ProcessInfo__from_process(ProcessPtr process)
{
    ProcessInfo info = (ProcessInfo) {
        .pid = process->pid,
    };
    KString state = kstring(ProcessState__to_string(process->state));
    KString no_wait_reason = KSTRING_NEW("-");
    KString class_name = ProcessClass__snapshot_name(process->process_class);

    ProcessInfo__copy_kstring(info.name, PROCESS_INFO_NAME_SIZE, &process->name);
    ProcessInfo__copy_kstring(info.state, PROCESS_INFO_STATE_SIZE, &state);
    Match(ParkedKernelContextPtr, Process__top_parked_context(process))
        IsSome(parked_context,
        {
            KString wait_reason = ParkReason__to_string(parked_context->park_reason);
            ProcessInfo__copy_kstring(info.wait_reason, PROCESS_INFO_WAIT_SIZE, &wait_reason);
        })
        IsNone(ProcessInfo__copy_kstring(info.wait_reason, PROCESS_INFO_WAIT_SIZE, &no_wait_reason))
    End
    ProcessInfo__copy_kstring(info.class_name, PROCESS_INFO_CLASS_SIZE, &class_name);
    return info;
}

Result(SystemCallResult) ProcessList__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_process_list_request, PROCESS_LIST_INVALID_ARGUMENT);
    }

    ProcessListRequestPtr request = (ProcessListRequestPtr) arg1;
    if ((Anything) request->entries == 0)
    {
        return Error(SystemCallResult, invalid_process_list_request, PROCESS_LIST_INVALID_ARGUMENT);
    }

    ProcessTable processes = Scheduler__process_snapshot(kernel->scheduler);
    uint64_t written = 0;
    FAIterate(ProcessTable, processes, process_index, process)
        if (written >= request->max_entries)
        {
            return Success(SystemCallResult, written);
        }
        request->entries[written] = ProcessInfo__from_process(process);
        written++;
    End

    return Success(SystemCallResult, written);
}

#include "process.h"

#include "syscall.h"
#include "thread_local.h"
#include "../newlib/allocator.h"
#include "syscalls/calls.h"

static uint64_t current_pid;

void thread_exit(void * result)
{
    if (ThreadLocal__destroy_current() != THREAD_OPERATION_OK)
    {
        __builtin_trap();
    }
    syscall(SYSCALL_THREAD_EXIT, (Anything)result, 0, 0, 0, 0);
    __builtin_trap();
}

static void thread_start_trampoline(ThreadStartRoutine start, void * argument)
{
    thread_exit(start(argument));
}

ThreadCreateResult thread_create(ThreadStartRoutine start, void * argument)
{
    match_result(
        ThreadLocal__new_thread(),
        success(block)
        {
            ThreadCreateResult result =
                (ThreadCreateResult)syscall(SYSCALL_THREAD_CREATE, (Anything)thread_start_trampoline, (Anything)start,
                                            (Anything)argument, block.thread_pointer, 0);
            if (result < 0)
            {
                ThreadLocal__release(&block);
            }
            return result;
        },
        error(code, message)
        {
            return code;
        }
    );
    return THREAD_OPERATION_NO_MEMORY;
}

Result(ThreadJoinResult) thread_join(ThreadID id)
{
    ThreadJoinResult joined = ThreadJoinResult__new(0);
    ThreadJoinStatus status = (ThreadJoinStatus)syscall(SYSCALL_THREAD_JOIN, id, (Anything)&joined, 0, 0, 0);
    if (status != THREAD_OPERATION_OK)
    {
        return Error(ThreadJoinResult, bstr("Thread join failed"), status);
    }
    return Success(ThreadJoinResult, joined);
}

Result(ProcessCredentials) process_credentials(uint64_t pid)
{
    ProcessCredentials credentials = ProcessCredentials__new(0, 0);
    int64_t status = (int64_t)syscall(SYSCALL_PROCESS_CREDENTIALS, pid, (Anything)&credentials, 0, 0, 0);
    if (status != PROCESS_CREDENTIALS_OK)
    {
        return Error(ProcessCredentials, bstr("Cannot resolve process credentials"), status);
    }
    return Success(ProcessCredentials, credentials);
}

Result(ProcessIdentity) process_identity(void)
{
    ProcessIdentity identity = ProcessIdentity__new(0, 0);
    int64_t status = (int64_t)syscall(SYSCALL_PROCESS_IDENTITY, (Anything)&identity, 0, 0, 0, 0);
    if (status != PROCESS_IDENTITY_OK)
    {
        return Error(ProcessIdentity, bstr("Cannot resolve process identity"), status);
    }
    return Success(ProcessIdentity, identity);
}

ProcessForkResult process_fork(void)
{
    Allocator__lock();
    ProcessForkResult result = (ProcessForkResult)syscall(SYSCALL_PROCESS_FORK, PROCESS_FORK_PREPARE, 0, 0, 0, 0);
    Allocator__unlock();
    return result;
}

ProcessForkResult process_fork_resume(uint64_t child_pid)
{
    return (ProcessForkResult)syscall(SYSCALL_PROCESS_FORK, PROCESS_FORK_RESUME, child_pid, 0, 0, 0);
}

ProcessForkResult process_fork_abort(uint64_t child_pid)
{
    return (ProcessForkResult)syscall(SYSCALL_PROCESS_FORK, PROCESS_FORK_ABORT, child_pid, 0, 0, 0);
}

void process_set_startup_pid(uint64_t pid)
{
    current_pid = pid;
}

uint64_t process_current_pid(void)
{
    if (current_pid == 0)
    {
        match_result(
            process_identity(),
            success(identity)
            {
                current_pid = identity.pid;
            },
            error(code, message)
            {
                return 0;
            }
        );
    }
    return current_pid;
}

Result(ConstProcessRegistryPtr) process_registry_map(void)
{
    ProcessRegistryMapResult result = (ProcessRegistryMapResult)syscall(SYSCALL_PROCESS_REGISTRY_MAP, 0, 0, 0, 0, 0);
    if (result < 0)
    {
        return Error(ConstProcessRegistryPtr, bstr("Cannot map process registry"), result);
    }
    return Success(ConstProcessRegistryPtr, (ConstProcessRegistryPtr)(uintptr_t)result);
}

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries)
{
    ProcessListRequest request = ProcessListRequest__new(entries, max_entries);
    return (ProcessListResult)syscall(SYSCALL_PROCESS_LIST, (Anything)&request, 0, 0, 0, 0);
}

ProcessSignalResult kill_process(int64_t target, ProcessSignal signal)
{
    return (ProcessSignalResult)syscall(SYSCALL_PROCESS_SIGNAL, (Anything)target, (Anything)signal, 0, 0, 0);
}

ProcessSignalResult terminal_interrupt_group(ProcessGroupID group)
{
    if (group == 0 || group > INT64_MAX)
    {
        return PROCESS_SIGNAL_INVALID_ARGUMENT;
    }
    return (ProcessSignalResult)syscall(SYSCALL_PROCESS_SIGNAL, (Anything) - (int64_t)group, PROCESS_SIGNAL_INTERRUPT,
                                        PROCESS_SIGNAL_ORIGIN_TERMINAL, 0, 0);
}

ProcessSignalResult process_signal_control(ProcessSignalControlRequestPtr request)
{
    return (ProcessSignalResult)syscall(SYSCALL_PROCESS_SIGNAL_CONTROL, (Anything)request, 0, 0, 0, 0);
}

ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    return wait_for_child_event_for(-1, 0, event);
}

ProcessChildEventResult wait_for_child_event_for(int64_t child_selection, uint64_t options, ProcessChildEventPtr event)
{
    return (ProcessChildEventResult)syscall(SYSCALL_PROCESS_WAIT_CHILD_EVENT, (Anything)event,
                                            (Anything)child_selection, (Anything)options, 0, 0);
}

ProcessTimesResult process_times(ProcessTimesPtr times)
{
    return (ProcessTimesResult)syscall(SYSCALL_PROCESS_TIMES, (Anything)times, 0, 0, 0, 0);
}

ProcessGroupResult process_group_control(ProcessGroupRequestPtr request)
{
    return (ProcessGroupResult)syscall(SYSCALL_PROCESS_GROUP, (Anything)request, 0, 0, 0, 0);
}

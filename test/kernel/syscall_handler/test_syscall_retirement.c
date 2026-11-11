#include "unity.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../../../kernel/syscall/init.h"

static ThreadPtr dispatched_thread;

static Result(SystemCallResult)
    Test__caller_pid(KernelPtr kernel, OptionalPtr(ProcessPtr) caller, Anything arg1, Anything arg2, Anything arg3,
                     Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(*caller_thread,
        some(thread)
        {
            dispatched_thread = thread;
        },
        none_do_nothing()
    );
    match_optional(
        *caller,
        some(process)
        {
            return Success(SystemCallResult, process->pid);
        },
        none()
        {
            return Error(SystemCallResult, bstr("Missing caller"), SYSCALL_FAILURE);
        }
    );
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_legacy_file_io_syscall_is_unregistered_and_rejected(void)
{
    KernelPtr kernel = init_test_kernel();
    match_result(Init__syscalls(&kernel->syscall_dispatcher),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Handlers__get(&kernel->syscall_dispatcher.handlers, IO_CALL)));
    Optional(ThreadPtr) thread = None(ThreadPtr);
    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &thread, IO_CALL, IO_OPEN, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code);
        }
    );
    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &thread, (SystemCall)0x22, 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code);
        }
    );
    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &thread, PING, 0, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SYSCALL_OK, code);
        },
        error_test_fail()
    );
}

void test_dispatch_derives_process_from_calling_thread(void)
{
    KernelPtr kernel = init_test_kernel();
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_READY);
    Thread sibling = Thread__new(process, &process->thread.stack);
    match_result(SyscallDispatcher__register(&kernel->syscall_dispatcher, PING, Test__caller_pid),
        success_do_nothing(),
                 error_test_fail()
    );

    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &Some(ThreadPtr, &sibling), PING, 0, 0, 0, 0,
                                    0),
        success(pid)
        {
            TEST_ASSERT_EQUAL_UINT64(process->pid, pid);
            TEST_ASSERT_EQUAL_PTR(&sibling, dispatched_thread);
        },
        error_test_fail()
    );
}

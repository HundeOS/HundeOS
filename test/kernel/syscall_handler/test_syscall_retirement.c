#include "unity.h"
#include "../fixtures/kernel.h"
#include "../../../kernel/syscall/init.h"

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
    Optional(ProcessPtr) process = None(ProcessPtr);
    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &process, IO_CALL, IO_OPEN, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code);
        }
    );
    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &process, (SystemCall)0x22, 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code);
        }
    );
    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &process, PING, 0, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SYSCALL_OK, code);
        },
        error_test_fail()
    );
}

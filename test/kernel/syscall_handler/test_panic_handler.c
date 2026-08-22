#include "unity.h"

#include "../../../kernel/syscall/init.h"
#include "../../../kernel/syscall/panic_handler.h"
#include "../../../kernel/syscall/syscall_dispatcher.h"
#include "../fixtures/kernel.h"
#include "../stubs/panic.h"

static KernelPtr kernel = NULL;

void setUp(void)
{
    clear_panic_state();
    kernel = init_test_kernel_without_filesystem();
}

void tearDown(void)
{
}

void test_panic_handler_triggers_kernel_panic(void)
{
    ResultMatch(SystemCallResult, Panic__handler(kernel, None(ProcessPtr), 0, 0, 0, 0, 0))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code))
    End

    TEST_ASSERT_TRUE(panic_occurred());
}

void test_init_registers_panic_syscall(void)
{
    ResultMatch(RegisterResult, Init__syscalls(&kernel->syscall_dispatcher))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, SyscallDispatcher__dispatch(
        &kernel->syscall_dispatcher, kernel, None(ProcessPtr), SYSCALL_PANIC, 0, 0, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code))
    End

    TEST_ASSERT_TRUE(panic_occurred());
}

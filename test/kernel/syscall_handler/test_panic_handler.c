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
    match_result(SystemCallResult, Panic__handler(kernel, None(ProcessPtr), 0, 0, 0, 0, 0), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code); });

    TEST_ASSERT_TRUE(panic_occurred());
}

void test_init_registers_panic_syscall(void)
{
    match_result(RegisterResult, Init__syscalls(&kernel->syscall_dispatcher), success_do_nothing(), error_test_fail());

    match_result(SystemCallResult, SyscallDispatcher__dispatch(
        &kernel->syscall_dispatcher, kernel, None(ProcessPtr), SYSCALL_PANIC, 0, 0, 0, 0, 0
    ), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code); });

    TEST_ASSERT_TRUE(panic_occurred());
}

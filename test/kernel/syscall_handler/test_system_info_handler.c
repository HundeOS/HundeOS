#include "unity.h"

#include "../../../kernel/syscall/system_info_handler.h"
#include "../../../common_includes/syscalls/system_info.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"

void setUp(void)
{
    clear_panic_state();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

static Kernel kernel_with_settings(bool io_latency_tracing_enabled)
{
    Kernel kernel = {};
    SystemInfo info = SystemInfo__new();
    info.io_latency_tracing_enabled = io_latency_tracing_enabled;
    kernel.system_info = Some(SystemInfo, info);
    return kernel;
}

void test_system_info_returns_settings_to_hund_process(void)
{
    Kernel kernel = kernel_with_settings(true);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;
    SystemSettings settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), (Anything)&settings, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_TRUE(settings.io_latency_tracing_enabled);
}

void test_system_info_rejects_non_hund_process_without_overwriting_output(void)
{
    Kernel kernel = kernel_with_settings(true);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_USER;
    SystemSettings settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), (Anything)&settings, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_PERMISSION_DENIED, code);
        }
    );

    TEST_ASSERT_FALSE(settings.io_latency_tracing_enabled);

    process->user_id = PROCESS_USER_ROOT;
    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), (Anything)&settings, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_PERMISSION_DENIED, code);
        }
    );
}

void test_system_info_rejects_missing_caller_and_output(void)
{
    Kernel kernel = kernel_with_settings(true);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;
    SystemSettings settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &None(ProcessPtr), (Anything)&settings, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_PERMISSION_DENIED, code);
        }
    );
    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_INVALID_ARGUMENT, code);
        }
    );

    TEST_ASSERT_FALSE(settings.io_latency_tracing_enabled);
}

void test_system_info_reports_unavailable_kernel_state(void)
{
    Kernel kernel = {};
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;
    SystemSettings settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), (Anything)&settings, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_UNAVAILABLE, code);
        }
    );

    TEST_ASSERT_FALSE(settings.io_latency_tracing_enabled);
}

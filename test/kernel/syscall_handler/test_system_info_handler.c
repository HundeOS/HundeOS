#include "unity.h"

#include "../../../kernel/syscall/system_info_handler.h"
#include "../../../kernel/trace_buffer.h"
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

typedef struct TestOutput
{
    Address user;
    Address kernel;
} TestOutput;

static TestOutput test_output(ProcessPtr process)
{
    UserAddress allocation = unwrap_result(Process__alloc_user_pages(process, PAGE_SIZE), NULL_USER_ADDRESS);
    return (TestOutput){.user = allocation.virtual_addr, .kernel = allocation.kernel_addr.virtual_addr};
}

void test_system_info_returns_settings_to_hund_process(void)
{
    Kernel kernel = kernel_with_settings(true);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;
    TestOutput output = test_output(process);
    SystemSettingsPtr settings = (SystemSettingsPtr)output.kernel;
    *settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_SETTINGS, output.user, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_TRUE(settings->io_latency_tracing_enabled);
}

void test_system_info_rejects_non_hund_process_without_overwriting_output(void)
{
    Kernel kernel = kernel_with_settings(true);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_USER;
    TestOutput output = test_output(process);
    SystemSettingsPtr settings = (SystemSettingsPtr)output.kernel;
    *settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_SETTINGS, output.user, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_PERMISSION_DENIED, code);
        }
    );

    TEST_ASSERT_FALSE(settings->io_latency_tracing_enabled);

    process->user_id = PROCESS_USER_ROOT;
    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_SETTINGS, output.user, 0, 0, 0),
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
    TestOutput output = test_output(process);
    SystemSettingsPtr settings = (SystemSettingsPtr)output.kernel;
    *settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &None(ProcessPtr), SYSTEM_INFO_READ_SETTINGS, output.user, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_PERMISSION_DENIED, code);
        }
    );
    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_SETTINGS, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_INVALID_ARGUMENT, code);
        }
    );

    TEST_ASSERT_FALSE(settings->io_latency_tracing_enabled);
}

void test_system_info_reports_unavailable_kernel_state(void)
{
    Kernel kernel = {};
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;
    TestOutput output = test_output(process);
    SystemSettingsPtr settings = (SystemSettingsPtr)output.kernel;
    *settings = SystemSettings__new(false);

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_SETTINGS, output.user, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_UNAVAILABLE, code);
        }
    );

    TEST_ASSERT_FALSE(settings->io_latency_tracing_enabled);
}

void test_system_info_copies_kernel_trace_to_valid_hund_memory(void)
{
    Kernel kernel = kernel_with_settings(false);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;
    TestOutput output = test_output(process);
    KernelTrace__append("system-info-trace-marker");

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_KERNEL_TRACE, output.user, PAGE_SIZE,
                            0, 0),
        success(size)
        {
            static const char marker[] = "system-info-trace-marker";
            TEST_ASSERT_GREATER_THAN_UINT64(0, size);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT64(sizeof(marker) - 1U, size);
            TEST_ASSERT_EQUAL_MEMORY(marker, (uint8_t *)output.kernel + size - (sizeof(marker) - 1U),
                                     sizeof(marker) - 1U);
        },
        error_test_fail()
    );
}

void test_system_info_rejects_unmapped_kernel_trace_destination(void)
{
    Kernel kernel = kernel_with_settings(false);
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    process->user_id = PROCESS_USER_HUND;

    match_result(
        SystemInfo__handler(&kernel, &Some(ProcessPtr, process), SYSTEM_INFO_READ_KERNEL_TRACE, 0x1234, 64, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_INVALID_ARGUMENT, code);
        }
    );
}

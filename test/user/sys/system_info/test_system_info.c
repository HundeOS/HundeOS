#include <string.h>

#include "unity.h"

#include "sys/system_info.h"
#include "syscalls/calls.h"

static SystemInfoResult syscall_result;
static bool returned_io_latency_tracing_enabled;
static SystemInfoOperation expected_operation;

SystemCallResult syscall(SystemCall number, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    TEST_ASSERT_EQUAL(SYSCALL_SYSTEM_INFO, number);
    TEST_ASSERT_EQUAL(expected_operation, arg1);
    if (expected_operation == SYSTEM_INFO_READ_SETTINGS)
    {
        SystemSettingsPtr settings = (SystemSettingsPtr)arg2;
        *settings = SystemSettings__new(returned_io_latency_tracing_enabled);
    }
    else if (syscall_result == SYSTEM_INFO_OK)
    {
        static const uint8_t trace[] = "trace";
        TEST_ASSERT_GREATER_OR_EQUAL_UINT64(sizeof(trace) - 1U, arg3);
        memcpy((void *)arg2, trace, sizeof(trace) - 1U);
        return sizeof(trace) - 1U;
    }
    return syscall_result;
}

void setUp(void)
{
    syscall_result = SYSTEM_INFO_OK;
    returned_io_latency_tracing_enabled = false;
    expected_operation = SYSTEM_INFO_READ_SETTINGS;
}

void test_kernel_trace_loads_snapshot(void)
{
    uint8_t destination[16] = {0};
    expected_operation = SYSTEM_INFO_READ_KERNEL_TRACE;

    match_result(
        KernelTrace__load(destination, sizeof(destination)),
        success(size)
        {
            TEST_ASSERT_EQUAL_UINT64(5, size);
            TEST_ASSERT_EQUAL_MEMORY("trace", destination, size);
        },
        error_test_fail()
    );
}

void tearDown(void)
{
}

void test_system_settings_loads_kernel_values(void)
{
    returned_io_latency_tracing_enabled = true;

    match_result(
        SystemSettings__load(),
        success(settings)
        {
            TEST_ASSERT_TRUE(settings.io_latency_tracing_enabled);
        },
        error_test_fail()
    );
}

void test_system_settings_returns_kernel_error(void)
{
    syscall_result = SYSTEM_INFO_PERMISSION_DENIED;

    match_result(
        SystemSettings__load(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSTEM_INFO_PERMISSION_DENIED, code);
        }
    );
}

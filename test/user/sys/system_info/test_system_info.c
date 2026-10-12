#include "unity.h"

#include "sys/system_info.h"
#include "syscalls/calls.h"

static SystemInfoResult syscall_result;
static bool returned_io_latency_tracing_enabled;

SystemCallResult syscall(SystemCall number, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    TEST_ASSERT_EQUAL(SYSCALL_SYSTEM_INFO, number);
    SystemSettingsPtr settings = (SystemSettingsPtr)arg1;
    *settings = SystemSettings__new(returned_io_latency_tracing_enabled);
    return syscall_result;
}

void setUp(void)
{
    syscall_result = SYSTEM_INFO_OK;
    returned_io_latency_tracing_enabled = false;
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

#include "unity.h"

#include "arch/init.h"
#include "boot.h"
#include "kernel_core.h"
#include "kernel_init.h"

static Kernel kernel;
static SystemInfo architecture_system_info;
static unsigned architecture_init_calls;

void setUp(void)
{
    kernel.system_info = None(SystemInfo);
    architecture_system_info = (SystemInfo){0};
    architecture_init_calls = 0;
}

void tearDown(void)
{
}

KernelPtr Kernel__instance(void)
{
    return &kernel;
}

void Arch__init_bootstrap_system(SystemInfoPtr system_info)
{
    architecture_system_info = *system_info;
    architecture_init_calls++;
}

void test_bootstrap_stores_system_info_and_configures_architecture(void)
{
    SystemInfo system_info = {0};
    system_info.cpu_count = 4;
    system_info.timer_frequency = 10000000;
    system_info.uart.virtual_base = 0x10000000;

    Boot__init_bootstrap_system(&system_info);

    TEST_ASSERT_EQUAL_UINT(1, architecture_init_calls);
    TEST_ASSERT_EQUAL_UINT64(4, architecture_system_info.cpu_count);
    TEST_ASSERT_EQUAL_UINT64(10000000, architecture_system_info.timer_frequency);
    TEST_ASSERT_EQUAL_HEX64(0x10000000, architecture_system_info.uart.virtual_base);

    system_info.cpu_count = 1;
    match_optional(
        kernel.system_info,
        some(installed)
        {
            TEST_ASSERT_EQUAL_UINT64(4, installed.cpu_count);
            TEST_ASSERT_EQUAL_UINT64(10000000, installed.timer_frequency);
            TEST_ASSERT_EQUAL_HEX64(0x10000000, installed.uart.virtual_base);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Bootstrap did not install system information");
        }
    );
}

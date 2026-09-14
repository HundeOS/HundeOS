#include "unity.h"
#include "process/process_registry.h"
ConstProcessRegistryPtr TestProcessRegistry__shared(void);
#define ProcessRegistry__shared TestProcessRegistry__shared
#include "../../../../user/lib/sys/process.c"
#undef ProcessRegistry__shared

static ProcessRegistry registry;
ConstProcessRegistryPtr TestProcessRegistry__shared(void)
{
    return &registry;
}

static int64_t credential_status;
static uint64_t requested_pid;

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    TEST_ASSERT_EQUAL(SYSCALL_PROCESS_CREDENTIALS, num);
    requested_pid = arg1;
    if (credential_status == PROCESS_CREDENTIALS_OK)
    {
        ProcessCredentialsPtr target = (ProcessCredentialsPtr)arg2;
        *target = ProcessCredentials__new(123, 456);
    }
    return credential_status;
}

void setUp(void)
{
    credential_status = PROCESS_CREDENTIALS_OK;
    requested_pid = 0;
    registry.version = PROCESS_REGISTRY_VERSION;
    ProcessRegistry__set_count(&registry, 0, 0);
}
void tearDown(void)
{
}

void test_process_credentials_returns_typed_kernel_identity(void)
{
    match_result(
        process_credentials(42),
        success(value)
        {
            TEST_ASSERT_EQUAL(42, requested_pid);
            TEST_ASSERT_EQUAL(123, value.user_id);
            TEST_ASSERT_EQUAL(456, value.group_id);
        },
        error_test_fail()
    );
}

void test_process_list_reads_shared_registry_without_a_syscall(void)
{
    ProcessRecord record = {.pid = 77,
                            .cpu = 3,
                            .name = bstr("shared"),
                            .state = PROC_STATE_RUNNING,
                            .wait_reason = None(ParkReason),
                            .credentials = ProcessCredentials__new(2, 2),
                            .cpu_usage = {.total_usec = 104900, .long_basis_points = 1049, .short_basis_points = 2500}};
    ProcessRegistry__set(&registry, 0, 0, &record);
    ProcessRegistry__set_count(&registry, 0, 1);
    ProcessInfo entries[2];
    TEST_ASSERT_EQUAL(1, process_list(entries, 2));
    TEST_ASSERT_EQUAL(77, entries[0].pid);
    TEST_ASSERT_EQUAL(3, entries[0].cpu);
    TEST_ASSERT_EQUAL(1049, entries[0].cpu_usage.long_basis_points);
    TEST_ASSERT_EQUAL_STRING("shared", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("PROC_STATE_RUNNING", entries[0].state);
    TEST_ASSERT_EQUAL_STRING("user", entries[0].user_name);
    TEST_ASSERT_EQUAL(0, requested_pid);
}

void test_process_credentials_does_not_fall_back_to_root_when_lookup_fails(void)
{
    credential_status = PROCESS_CREDENTIALS_NOT_FOUND;
    match_result(
        process_credentials(42),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(PROCESS_CREDENTIALS_NOT_FOUND, code); }
    );
}

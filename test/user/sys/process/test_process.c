#include "unity.h"
#include "process/process_registry.h"
#include "../../../../user/lib/sys/process.c"

static int64_t credential_status;
static uint64_t requested_pid;
static ProcessInfo listed_process;
static ProcessListResult list_status;
static uint64_t list_capacity;
static unsigned list_calls;
static ProcessChildEventResult child_event_status;
static ProcessChildEvent child_event;
static int64_t requested_child_pid;
static uint64_t requested_wait_options;
static ProcessTimesResult times_status;
static ProcessTimes configured_times;
static ProcessIdentityResult identity_status;
static ProcessIdentity configured_identity;
static ProcessForkResult fork_status;
static ProcessForkOperation fork_operation;
static uint64_t fork_child_pid;
static ProcessGroupResult process_group_status;
static ProcessGroupRequest process_group_reply;
static ProcessRegistryMapResult process_registry_map_result;

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    if (num == SYSCALL_PROCESS_LIST)
    {
        ProcessListRequestPtr request = (ProcessListRequestPtr)arg1;
        list_capacity = request->max_entries;
        list_calls++;
        if (list_status > 0)
        {
            request->entries[0] = listed_process;
        }
        return list_status;
    }
    if (num == SYSCALL_PROCESS_WAIT_CHILD_EVENT)
    {
        requested_child_pid = arg2;
        requested_wait_options = arg3;
        if (child_event_status == PROCESS_CHILD_EVENT_OK)
        {
            ProcessChildEventPtr target = (ProcessChildEventPtr)arg1;
            *target = child_event;
        }
        return child_event_status;
    }
    if (num == SYSCALL_PROCESS_TIMES)
    {
        if (times_status == PROCESS_TIMES_OK)
        {
            ProcessTimesPtr target = (ProcessTimesPtr)arg1;
            *target = configured_times;
        }
        return times_status;
    }
    if (num == SYSCALL_PROCESS_IDENTITY)
    {
        if (identity_status == PROCESS_IDENTITY_OK)
        {
            ProcessIdentityPtr target = (ProcessIdentityPtr)arg1;
            *target = configured_identity;
        }
        return identity_status;
    }
    if (num == SYSCALL_PROCESS_FORK)
    {
        fork_operation = (ProcessForkOperation)arg1;
        fork_child_pid = arg2;
        return fork_status;
    }
    if (num == SYSCALL_PROCESS_GROUP)
    {
        ProcessGroupRequestPtr request = (ProcessGroupRequestPtr)arg1;
        process_group_reply = *request;
        request->result_id = 71;
        return process_group_status;
    }
    if (num == SYSCALL_PROCESS_REGISTRY_MAP)
    {
        return process_registry_map_result;
    }
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
    listed_process = ProcessInfo__new(0);
    list_status = 1;
    list_capacity = 0;
    list_calls = 0;
    child_event_status = PROCESS_CHILD_EVENT_OK;
    child_event = (ProcessChildEvent){
        .pid = 42,
        .status = PROCESS_WAIT_STATUS_EXITED(7),
        .type = PROCESS_CHILD_EVENT_EXITED,
    };
    requested_child_pid = 99;
    requested_wait_options = 99;
    times_status = PROCESS_TIMES_OK;
    configured_times = (ProcessTimes){
        .elapsed_usec = 100,
        .user_usec = 20,
        .system_usec = 10,
        .children_user_usec = 7,
        .children_system_usec = 3,
    };
    identity_status = PROCESS_IDENTITY_OK;
    configured_identity = ProcessIdentity__new(42, 21);
    fork_status = 73;
    fork_operation = 0;
    fork_child_pid = 0;
    process_group_status = PROCESS_GROUP_OK;
    process_group_reply = (ProcessGroupRequest){0};
    process_registry_map_result = PROCESS_REGISTRY_VADDR;
}

void test_process_registry_map_returns_kernel_selected_address(void)
{
    match_result(
        process_registry_map(),
        success(registry)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_REGISTRY_VADDR, (uintptr_t)registry);
        },
        error_test_fail()
    );
}

void test_process_registry_map_propagates_syscall_error(void)
{
    process_registry_map_result = PROCESS_REGISTRY_MAP_FAILED;
    match_result(
        process_registry_map(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_REGISTRY_MAP_FAILED, code);
        }
    );
}

void test_process_fork_returns_typed_syscall_result(void)
{
    TEST_ASSERT_EQUAL_INT64(73, process_fork());
    TEST_ASSERT_EQUAL(PROCESS_FORK_PREPARE, fork_operation);
    TEST_ASSERT_EQUAL_UINT64(0, fork_child_pid);

    TEST_ASSERT_EQUAL_INT64(73, process_fork_resume(91));
    TEST_ASSERT_EQUAL(PROCESS_FORK_RESUME, fork_operation);
    TEST_ASSERT_EQUAL_UINT64(91, fork_child_pid);

    TEST_ASSERT_EQUAL_INT64(73, process_fork_abort(92));
    TEST_ASSERT_EQUAL(PROCESS_FORK_ABORT, fork_operation);
    TEST_ASSERT_EQUAL_UINT64(92, fork_child_pid);
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

void test_process_list_requests_live_snapshot_through_syscall(void)
{
    ProcessRecord record = {.pid = 77,
                            .cpu = 3,
                            .name = bstr("shared"),
                            .state = PROC_STATE_RUNNING,
                            .wait_reason = None(ParkReason),
                            .credentials = ProcessCredentials__new(2, 2),
                            .cpu_usage = {.total_usec = 104900, .long_basis_points = 1049, .short_basis_points = 2500}};
    listed_process = ProcessRecord__info(&record);
    ProcessInfo entries[2];
    TEST_ASSERT_EQUAL(1, process_list(entries, 2));
    TEST_ASSERT_EQUAL(77, entries[0].pid);
    TEST_ASSERT_EQUAL(3, entries[0].cpu);
    TEST_ASSERT_EQUAL(1049, entries[0].cpu_usage.long_basis_points);
    TEST_ASSERT_EQUAL_STRING("shared", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("PROC_STATE_RUNNING", entries[0].state);
    TEST_ASSERT_EQUAL_STRING("user", entries[0].user_name);
    TEST_ASSERT_EQUAL(0, requested_pid);
    TEST_ASSERT_EQUAL(1, list_calls);
    TEST_ASSERT_EQUAL(2, list_capacity);
}

void test_process_list_propagates_syscall_error(void)
{
    ProcessInfo entries[1];
    list_status = PROCESS_LIST_INVALID_ARGUMENT;
    TEST_ASSERT_EQUAL(PROCESS_LIST_INVALID_ARGUMENT, process_list(entries, 1));
    TEST_ASSERT_EQUAL(1, list_calls);
}

void test_process_credentials_does_not_fall_back_to_root_when_lookup_fails(void)
{
    credential_status = PROCESS_CREDENTIALS_NOT_FOUND;
    match_result(
        process_credentials(42),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CREDENTIALS_NOT_FOUND, code);
        }
    );
}

void test_wait_for_child_event_transports_selection_and_options(void)
{
    ProcessChildEvent event = ProcessChildEvent__new();

    TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, wait_for_child_event_for(42, PROCESS_CHILD_WAIT_NOHANG, &event));
    TEST_ASSERT_EQUAL_UINT64(42, requested_child_pid);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_CHILD_WAIT_NOHANG, requested_wait_options);
    TEST_ASSERT_EQUAL_UINT64(child_event.pid, event.pid);
    TEST_ASSERT_EQUAL_INT64(child_event.status, event.status);
}

void test_wait_for_child_event_preserves_legacy_any_child_blocking_call(void)
{
    ProcessChildEvent event = ProcessChildEvent__new();

    TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, wait_for_child_event(&event));
    TEST_ASSERT_EQUAL_INT64(-1, requested_child_pid);
    TEST_ASSERT_EQUAL_UINT64(0, requested_wait_options);
}

void test_process_group_control_transports_request_and_result(void)
{
    ProcessGroupRequest request = {
        .operation = PROCESS_GROUP_SET,
        .pid = 42,
        .process_group_id = 71,
    };
    TEST_ASSERT_EQUAL(PROCESS_GROUP_OK, process_group_control(&request));
    TEST_ASSERT_EQUAL(PROCESS_GROUP_SET, process_group_reply.operation);
    TEST_ASSERT_EQUAL_INT64(42, process_group_reply.pid);
    TEST_ASSERT_EQUAL_INT64(71, request.result_id);
}

void test_process_times_transports_kernel_snapshot(void)
{
    ProcessTimes actual = ProcessTimes__new();

    TEST_ASSERT_EQUAL(PROCESS_TIMES_OK, process_times(&actual));
    TEST_ASSERT_EQUAL_UINT64(configured_times.elapsed_usec, actual.elapsed_usec);
    TEST_ASSERT_EQUAL_UINT64(configured_times.user_usec, actual.user_usec);
    TEST_ASSERT_EQUAL_UINT64(configured_times.system_usec, actual.system_usec);
    TEST_ASSERT_EQUAL_UINT64(configured_times.children_user_usec, actual.children_user_usec);
    TEST_ASSERT_EQUAL_UINT64(configured_times.children_system_usec, actual.children_system_usec);
}

void test_process_identity_transports_kernel_identity(void)
{
    match_result(
        process_identity(),
        success(identity)
        {
            TEST_ASSERT_EQUAL_UINT64(configured_identity.pid, identity.pid);
            TEST_ASSERT_EQUAL_UINT64(configured_identity.parent_pid, identity.parent_pid);
        },
        error_test_fail()
    );
}

void test_process_identity_propagates_syscall_error(void)
{
    identity_status = PROCESS_IDENTITY_INVALID_ARGUMENT;

    match_result(
        process_identity(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDENTITY_INVALID_ARGUMENT, code);
        }
    );
}

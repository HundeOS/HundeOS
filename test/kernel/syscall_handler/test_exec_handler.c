#include "unity.h"

#include "../../../common_includes/syscalls/spawn.h"
#include "../../../kernel/syscall/auth.h"
#include "../../../kernel/syscall/exec_handler.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/process.h"

#define Exec__handler(kernel, proc, ...) Exec__handler((kernel), &(proc), __VA_ARGS__)
#define ExecRequest__handler(kernel, proc, ...) ExecRequest__handler((kernel), &(proc), __VA_ARGS__)

static KernelPtr kernel = NULL;
static ProcessPtr parent = NULL;
static BString exec_path = bstr("/sbin/hello_world");
static const CPUID cpu = 0;

static SpawnRequest test_spawn_request(
    const char * raw_path, SpawnMode mode, AuthTicketID auth_ticket_id, SpawnStdio stdio
)
{
    BString path = bstring(raw_path);
    SpawnArguments arguments = SpawnArguments__new();
    return SpawnRequest__new(
        mode,
        &path,
        SPAWN_INHERIT_USER_ID,
        auth_ticket_id,
        stdio,
        &arguments
    );
}

static UserAllocation allocation_at(ProcessPtr process, uint64_t index)
{
    match_optional(UserAllocation, UserAllocations__at(&process->user_allocations, index),
        some(allocation) { return allocation; },
        none() { TEST_FAIL(); }
    );

    return (UserAllocation) { 0 };
}

void setUp(void)
{
    clear_deferred_syscall_result();
    current_process_reset();
    kernel = init_test_kernel();
    parent = dummy_process(0, cpu, PROC_STATE_RUNNING);
    Process__set_hund_identity(parent);
    Scheduler__schedule(kernel->scheduler, parent);
    Scheduler__preempt(kernel->scheduler, cpu, NULL);
}

void tearDown(void)
{

}

void test_exec_is_called_triggering_new_process()
{
    SpawnRequest request = test_spawn_request(cstr(exec_path), P_WAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);

    match_result(SystemCallResult, Exec__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);
}

void test_exec_handler_nowait_spawns_without_parking_parent()
{
    SpawnRequest request = test_spawn_request(cstr(exec_path), P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);

    match_result(SystemCallResult, Exec__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
}

void test_exec_request_handler_passes_spawn_arguments_to_process_argv(void)
{
    SpawnArguments arguments = SpawnArguments__new();
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("one")), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("two")), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("three")), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("four")), success_do_nothing(), error_test_fail());
    BString path = bstring("/sbin/hello_world");
    SpawnRequest request = SpawnRequest__new(
        P_NOWAIT,
        &path,
        SPAWN_INHERIT_USER_ID,
        SPAWN_INHERIT_AUTH_TICKET,
        SPAWN_STDIO_DEFAULT,
        &arguments
    );

    match_result(SystemCallResult, ExecRequest__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(pid) {
            match_optional(ProcessPtr, Scheduler__find_process_by_pid(kernel->scheduler, pid), some(child) {
                    UserAllocation args = allocation_at(child, 0);
                    Address * argv = ptr(args.address.kernel_addr.virtual_addr, Address);
                    Address user_base = args.address.virtual_addr;
                    Address kernel_base = args.address.kernel_addr.virtual_addr;

                    TEST_ASSERT_EQUAL(0, argv[5]);
                    TEST_ASSERT_EQUAL_STRING("/sbin/hello_world", ptr(kernel_base + (argv[0] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("one", ptr(kernel_base + (argv[1] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("two", ptr(kernel_base + (argv[2] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("three", ptr(kernel_base + (argv[3] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("four", ptr(kernel_base + (argv[4] - user_base), char));
                }, none_test_fail());
        }, error_test_fail());
}

void test_exec_request_handler_can_spawn_as_user(void)
{
    Process__set_hund_identity(parent);
    BString username = bstring("user");
    BString password = bstring("user");
    Result(AuthTicketID) auth_result = Auth__authenticate(&kernel->auth, parent, &bstr("user"), &bstr("user"));
    AuthTicketID ticket_id = result_unwrap(AuthTicketID, auth_result, AUTH_NO_TICKET);

    SpawnRequest request = test_spawn_request(
        "/sbin/shell",
        P_NOWAIT,
        ticket_id,
        SPAWN_STDIO_DEFAULT
    );

    match_result(SystemCallResult, ExecRequest__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());
}

void test_exec_request_handler_rejects_reused_auth_ticket(void)
{
    BString username = bstring("user");
    BString password = bstring("user");
    Result(AuthTicketID) auth_result = Auth__authenticate(&kernel->auth, parent, &bstr("user"), &bstr("user"));
    AuthTicketID ticket_id = result_unwrap(AuthTicketID, auth_result, AUTH_NO_TICKET);

    SpawnRequest request = test_spawn_request(
        "/sbin/shell",
        P_NOWAIT,
        ticket_id,
        SPAWN_STDIO_DEFAULT
    );

    match_result(SystemCallResult, ExecRequest__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());

    match_result(SystemCallResult, ExecRequest__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(AUTH_TICKET_CONSUMED, code); });
}

void test_exec_request_handler_can_spawn_root_shell_with_auth_ticket(void)
{
    Process__set_hund_identity(parent);
    Result(AuthTicketID) auth_result = Auth__authenticate(&kernel->auth, parent, &bstr("root"), &bstr("root"));
    AuthTicketID ticket_id = result_unwrap(AuthTicketID, auth_result, AUTH_NO_TICKET);

    SpawnRequest request = test_spawn_request(
        "/sbin/shell",
        P_NOWAIT,
        ticket_id,
        SPAWN_STDIO_DEFAULT
    );

    match_result(SystemCallResult, ExecRequest__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());
}

void test_exec_request_handler_can_spawn_with_explicit_stdio(void)
{
    Process__set_file_descriptor(parent, 7, 0x7000);
    Process__set_file_descriptor(parent, 8, 0x8000);
    Process__set_file_descriptor(parent, 9, 0x9000);

    SpawnRequest request = test_spawn_request(
        "/sbin/shell",
        P_NOWAIT,
        SPAWN_INHERIT_AUTH_TICKET,
        SpawnStdio__new(7, 8, 9)
    );

    match_result(SystemCallResult, ExecRequest__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) &request,
        0,
        0,
        0,
        0
    ), success(pid) {
            match_optional(ProcessPtr, Scheduler__find_process_by_pid(kernel->scheduler, pid), some(child) {
                    match_result(ProcessIOHandle, Process__get_io_handle(child, 0), success(handle) { TEST_ASSERT_EQUAL(0x7000, handle); }, error_test_fail());
                    match_result(ProcessIOHandle, Process__get_io_handle(child, 1), success(handle) { TEST_ASSERT_EQUAL(0x8000, handle); }, error_test_fail());
                    match_result(ProcessIOHandle, Process__get_io_handle(child, 2), success(handle) { TEST_ASSERT_EQUAL(0x9000, handle); }, error_test_fail());
                }, none_test_fail());
        }, error_test_fail());
}

#include "unity.h"

#include "../../../common_includes/syscalls/spawn.h"
#include "../../../kernel/syscall/auth.h"
#include "../../../kernel/syscall/exec_handler.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/process.h"

static KernelPtr kernel = NULL;
static ProcessPtr parent = NULL;
static BString exec_path = BSTRING_NEW("/sbin/hello_world");
static const CPUID cpu = 0;

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
    match_result(SystemCallResult, Exec__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) cstr(exec_path),
        P_WAIT,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);
}

void test_exec_handler_nowait_spawns_without_parking_parent()
{
    match_result(SystemCallResult, Exec__handler(
        kernel,
        Some(ProcessPtr, parent),
        (Anything) cstr(exec_path),
        P_NOWAIT,
        0,
        0,
        0
    ), success(code) { TEST_ASSERT_GREATER_THAN(0, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
}

void test_exec_request_handler_can_spawn_as_user(void)
{
    Process__set_hund_identity(parent);
    AuthTicketID ticket_id = result_unwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, parent, bstring("user"), bstring("user")),
        AUTH_NO_TICKET
    );

    SpawnRequest request = (SpawnRequest) {
        .path = "/sbin/shell",
        .mode = P_NOWAIT,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = ticket_id,
        .arg0 = 0,
        .arg1 = 0,
        .arg2 = 0,
    };

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
    AuthTicketID ticket_id = result_unwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, parent, bstring("user"), bstring("user")),
        AUTH_NO_TICKET
    );

    SpawnRequest request = (SpawnRequest) {
        .path = "/sbin/shell",
        .mode = P_NOWAIT,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = ticket_id,
        .arg0 = 0,
        .arg1 = 0,
        .arg2 = 0,
    };

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
    AuthTicketID ticket_id = result_unwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, parent, bstring("root"), bstring("root")),
        AUTH_NO_TICKET
    );

    SpawnRequest request = (SpawnRequest) {
        .path = "/sbin/shell",
        .mode = P_NOWAIT,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = ticket_id,
        .arg0 = 0,
        .arg1 = 0,
        .arg2 = 0,
    };

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

    SpawnRequest request = (SpawnRequest) {
        .path = "/sbin/shell",
        .mode = P_NOWAIT,
        .requested_user_id = SPAWN_INHERIT_USER_ID,
        .auth_ticket_id = SPAWN_INHERIT_AUTH_TICKET,
        .stdio = SpawnStdio__new(7, 8, 9),
        .arg0 = 0,
        .arg1 = 0,
        .arg2 = 0,
    };

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

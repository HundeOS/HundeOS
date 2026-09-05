#include "unity.h"

#include "../../../kernel/syscall/auth.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"

static KernelPtr kernel = NULL;
static ProcessPtr process = NULL;

void setUp(void)
{
    kernel = init_test_kernel();
    process = dummy_process(77, 0, PROC_STATE_RUNNING);
}

void tearDown(void)
{
}

void test_auth_authenticates_root_and_issues_ticket(void)
{
    BString username = bstring("root");
    BString password = bstring("root");
    match_result(AuthTicketID, Auth__authenticate(&kernel->auth, process, &bstr("root"), &bstr("root")), success(ticket_id) { TEST_ASSERT_GREATER_THAN(AUTH_NO_TICKET, ticket_id); }, error_test_fail());
}

void test_auth_rejects_invalid_credentials(void)
{
    BString username = bstring("root");
    BString password = bstring("bad");
    match_result(AuthTicketID, Auth__authenticate(&kernel->auth, process, &bstr("root"), &bstr("bad")), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(AUTH_INVALID_CREDENTIALS, code); });
}

void test_auth_consumes_ticket_once(void)
{
    BString username = bstring("hund");
    BString password = bstring("hund");
    AuthTicketID ticket_id = result_unwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, process, &bstr("hund"), &bstr("hund")),
        AUTH_NO_TICKET
    );

    match_result(UserID, Auth__consume_ticket(&kernel->auth, process, ticket_id), success(user_id) { TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, user_id); }, error_test_fail());

    match_result(UserID, Auth__consume_ticket(&kernel->auth, process, ticket_id), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(AUTH_TICKET_CONSUMED, code); });
}

void test_auth_rejects_ticket_for_different_process(void)
{
    ProcessPtr other_process = dummy_process(78, 0, PROC_STATE_RUNNING);
    AuthTicketID ticket_id = result_unwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, process, &bstr("user"), &bstr("user")),
        AUTH_NO_TICKET
    );

    match_result(UserID, Auth__consume_ticket(&kernel->auth, other_process, ticket_id), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(AUTH_TICKET_FOR_DIFFERENT_PROCESS, code); });
}

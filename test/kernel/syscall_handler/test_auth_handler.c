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
    ResultMatch(AuthTicketID, Auth__authenticate(&kernel->auth, process, bstring("root"), bstring("root")))
        IsSuccess(ticket_id, TEST_ASSERT_GREATER_THAN(AUTH_NO_TICKET, ticket_id))
        IsError(code, message, TEST_FAIL())
    End
}

void test_auth_rejects_invalid_credentials(void)
{
    ResultMatch(AuthTicketID, Auth__authenticate(&kernel->auth, process, bstring("root"), bstring("bad")))
        IsSuccess(ticket_id, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(AUTH_INVALID_CREDENTIALS, code))
    End
}

void test_auth_consumes_ticket_once(void)
{
    AuthTicketID ticket_id = ResultUnwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, process, bstring("hund"), bstring("hund")),
        AUTH_NO_TICKET
    );

    ResultMatch(UserID, Auth__consume_ticket(&kernel->auth, process, ticket_id))
        IsSuccess(user_id, TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, user_id))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(UserID, Auth__consume_ticket(&kernel->auth, process, ticket_id))
        IsSuccess(user_id, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(AUTH_TICKET_CONSUMED, code))
    End
}

void test_auth_rejects_ticket_for_different_process(void)
{
    ProcessPtr other_process = dummy_process(78, 0, PROC_STATE_RUNNING);
    AuthTicketID ticket_id = ResultUnwrap(
        AuthTicketID,
        Auth__authenticate(&kernel->auth, process, bstring("user"), bstring("user")),
        AUTH_NO_TICKET
    );

    ResultMatch(UserID, Auth__consume_ticket(&kernel->auth, other_process, ticket_id))
        IsSuccess(user_id, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(AUTH_TICKET_FOR_DIFFERENT_PROCESS, code))
    End
}

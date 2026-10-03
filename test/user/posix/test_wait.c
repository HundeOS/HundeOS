#include "unity.h"

#include <errno.h>

#include "../../../common_includes/syscalls/process.h"
#include "../../../user/lib/newlib/include/sys/wait.h"

static ProcessChildEventResult wait_result;
static ProcessChildEvent next_event;
static int64_t requested_child;
static uint64_t requested_options;
static unsigned int wait_calls;

ProcessChildEventResult wait_for_child_event_for(int64_t child_pid, uint64_t options, ProcessChildEventPtr event)
{
    wait_calls++;
    requested_child = child_pid;
    requested_options = options;
    if (wait_result == PROCESS_CHILD_EVENT_OK)
    {
        *event = next_event;
    }
    return wait_result;
}

#include "../../../user/lib/newlib/wait.c"

void setUp(void)
{
    wait_result = PROCESS_CHILD_EVENT_OK;
    next_event = (ProcessChildEvent){
        .pid = 42,
        .status = PROCESS_WAIT_STATUS_EXITED(7),
        .type = PROCESS_CHILD_EVENT_EXITED,
    };
    requested_child = 99;
    requested_options = 99;
    wait_calls = 0;
    errno = 0;
}

void tearDown(void)
{
}

void test_wait_returns_any_exited_child_and_posix_status(void)
{
    int status = -1;

    TEST_ASSERT_EQUAL_INT64(42, wait(&status));
    TEST_ASSERT_EQUAL_INT64(-1, requested_child);
    TEST_ASSERT_EQUAL_UINT64(0, requested_options);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(7, WEXITSTATUS(status));
}

void test_waitpid_requests_selected_child_and_options(void)
{
    next_event.status = PROCESS_WAIT_STATUS_SIGNALED(9);
    int status = 0;

    TEST_ASSERT_EQUAL_INT64(42, waitpid(42, &status, WNOHANG | WUNTRACED));
    TEST_ASSERT_EQUAL_UINT64(42, requested_child);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_CHILD_WAIT_NOHANG | PROCESS_CHILD_WAIT_UNTRACED, requested_options);
    TEST_ASSERT_TRUE(WIFSIGNALED(status));
    TEST_ASSERT_EQUAL_INT(9, WTERMSIG(status));
}

void test_waitpid_nohang_returns_zero_without_changing_status(void)
{
    wait_result = PROCESS_CHILD_EVENT_PENDING;
    int status = 1234;

    TEST_ASSERT_EQUAL_INT64(0, waitpid(-1, &status, WNOHANG));
    TEST_ASSERT_EQUAL_INT(1234, status);
}

void test_waitpid_maps_no_child_to_echild(void)
{
    wait_result = PROCESS_CHILD_EVENT_NO_CHILD;

    TEST_ASSERT_EQUAL_INT64(-1, waitpid(42, NULL, 0));
    TEST_ASSERT_EQUAL_INT(ECHILD, errno);
}

void test_waitpid_maps_signal_interruption_to_eintr(void)
{
    wait_result = (ProcessChildEventResult)SYSCALL_INTERRUPTED;

    TEST_ASSERT_EQUAL_INT64(-1, waitpid(-1, NULL, 0));
    TEST_ASSERT_EQUAL_INT(EINTR, errno);
}

void test_waitpid_transports_process_group_selectors_and_rejects_unknown_options(void)
{
    TEST_ASSERT_EQUAL_INT64(42, waitpid(0, NULL, 0));
    TEST_ASSERT_EQUAL_INT64(0, requested_child);
    TEST_ASSERT_EQUAL_INT64(42, waitpid(-7, NULL, 0));
    TEST_ASSERT_EQUAL_INT64(-7, requested_child);
    TEST_ASSERT_EQUAL_UINT(2, wait_calls);

    TEST_ASSERT_EQUAL_INT64(-1, waitpid(-1, NULL, 0x40));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_UINT(2, wait_calls);
}

void test_wait_status_macros_recognize_stopped_and_continued(void)
{
    int stopped = (int)PROCESS_WAIT_STATUS_STOPPED(19);

    TEST_ASSERT_TRUE(WIFSTOPPED(stopped));
    TEST_ASSERT_EQUAL_INT(19, WSTOPSIG(stopped));
    TEST_ASSERT_TRUE(WIFCONTINUED(PROCESS_WAIT_STATUS_CONTINUED));
}

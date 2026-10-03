#include <errno.h>
#include <unistd.h>

#include "unity.h"

#include "../../../common_includes/syscalls/process.h"
#include "../../../common_includes/syscalls/io.h"

static ProcessForkResult fork_result;
static ProcessIdentityResult identity_result;
static ProcessIdentity identity;
static uint64_t recorded_pid;
static unsigned sequence;
static unsigned prepare_sequence;
static unsigned commit_sequence;
static unsigned resume_sequence;
static unsigned abort_calls;
static IOStatus prepare_status;
static IOStatus commit_status;

IOStatus PosixIO__prepare_fork(void)
{
    prepare_sequence = ++sequence;
    return prepare_status;
}

IOStatus PosixIO__commit_fork(uint64_t child_pid)
{
    commit_sequence = ++sequence;
    return commit_status;
}

void PosixIO__abort_fork(void)
{
}

IOStatus PosixIO__claim_fork(void)
{
    return IO_OK;
}

void PosixIO__finish_fork(void)
{
}

ProcessForkResult process_fork(void)
{
    return fork_result;
}

ProcessForkResult process_fork_resume(uint64_t child_pid)
{
    resume_sequence = ++sequence;
    return (ProcessForkResult)child_pid;
}

ProcessForkResult process_fork_abort(uint64_t child_pid)
{
    abort_calls++;
    return 0;
}

Result(ProcessIdentity) process_identity(void)
{
    if (identity_result != PROCESS_IDENTITY_OK)
    {
        return Error(ProcessIdentity, bstr("Missing identity"), identity_result);
    }
    return Success(ProcessIdentity, identity);
}

void process_set_startup_pid(uint64_t pid)
{
    recorded_pid = pid;
}

#include "../../../user/lib/newlib/fork.c"

void setUp(void)
{
    fork_result = 42;
    identity_result = PROCESS_IDENTITY_OK;
    identity = ProcessIdentity__new(84, 42);
    recorded_pid = 0;
    sequence = 0;
    prepare_sequence = 0;
    commit_sequence = 0;
    resume_sequence = 0;
    abort_calls = 0;
    prepare_status = IO_OK;
    commit_status = IO_OK;
    errno = 0;
}

void tearDown(void)
{
}

void test_fork_returns_child_pid_to_parent(void)
{
    TEST_ASSERT_EQUAL_INT64(42, fork());
    TEST_ASSERT_EQUAL_UINT64(0, recorded_pid);
    TEST_ASSERT_LESS_THAN(commit_sequence, prepare_sequence);
    TEST_ASSERT_LESS_THAN(resume_sequence, commit_sequence);
}

void test_fork_aborts_unpublished_child_when_inheritance_commit_fails(void)
{
    commit_status = IO_REGISTER_FAILED;

    TEST_ASSERT_EQUAL_INT64(-1, fork());
    TEST_ASSERT_EQUAL_INT(EIO, errno);
    TEST_ASSERT_EQUAL_UINT(1, abort_calls);
    TEST_ASSERT_EQUAL_UINT(0, resume_sequence);
}

void test_fork_refreshes_cached_pid_in_child(void)
{
    fork_result = 0;

    TEST_ASSERT_EQUAL_INT64(0, fork());
    TEST_ASSERT_EQUAL_UINT64(84, recorded_pid);
}

void test_fork_maps_kernel_failures_to_errno(void)
{
    fork_result = PROCESS_FORK_NO_MEMORY;
    TEST_ASSERT_EQUAL_INT64(-1, fork());
    TEST_ASSERT_EQUAL_INT(ENOMEM, errno);

    fork_result = PROCESS_FORK_UNSUPPORTED_STATE;
    TEST_ASSERT_EQUAL_INT64(-1, fork());
    TEST_ASSERT_EQUAL_INT(EOPNOTSUPP, errno);

    fork_result = PROCESS_FORK_SCHEDULE_FAILED;
    TEST_ASSERT_EQUAL_INT64(-1, fork());
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
}

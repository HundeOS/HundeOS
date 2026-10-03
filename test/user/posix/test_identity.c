#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
#include <unistd.h>

#include "unity.h"

#include "../../../common_includes/syscalls/process.h"

static uint64_t current_pid;
static uint64_t requested_pid;
static ProcessCredentials current_credentials;
static ProcessCredentialsStatus credential_status;
static ProcessIdentity current_identity;
static ProcessIdentityResult identity_status;
static ProcessGroupResult process_group_status;
static ProcessGroupRequest last_process_group_request;

uint64_t process_current_pid(void)
{
    return current_pid;
}

Result(ProcessCredentials) process_credentials(uint64_t pid)
{
    requested_pid = pid;
    if (credential_status != PROCESS_CREDENTIALS_OK)
    {
        return Error(ProcessCredentials, bstr("Missing process credentials"), credential_status);
    }
    return Success(ProcessCredentials, current_credentials);
}

Result(ProcessIdentity) process_identity(void)
{
    if (identity_status != PROCESS_IDENTITY_OK)
    {
        return Error(ProcessIdentity, bstr("Missing process identity"), identity_status);
    }
    return Success(ProcessIdentity, current_identity);
}

ProcessGroupResult process_group_control(ProcessGroupRequestPtr request)
{
    last_process_group_request = *request;
    if (process_group_status == PROCESS_GROUP_OK)
    {
        request->result_id = request->operation == PROCESS_GROUP_CREATE_SESSION ? 42 : 7;
    }
    return process_group_status;
}

#include "../../../user/lib/newlib/identity.c"

void setUp(void)
{
    current_pid = 42;
    requested_pid = 0;
    current_credentials = ProcessCredentials__new(PROCESS_USER_HUND, PROCESS_GROUP_USER);
    credential_status = PROCESS_CREDENTIALS_OK;
    current_identity = ProcessIdentity__new(42, 21);
    identity_status = PROCESS_IDENTITY_OK;
    process_group_status = PROCESS_GROUP_OK;
    last_process_group_request = (ProcessGroupRequest){0};
    errno = 0;
}

void tearDown(void)
{
}

void test_posix_identity_getters_return_current_kernel_credentials(void)
{
    TEST_ASSERT_EQUAL(PROCESS_USER_HUND, getuid());
    TEST_ASSERT_EQUAL(42, requested_pid);
    TEST_ASSERT_EQUAL(PROCESS_USER_HUND, geteuid());
    TEST_ASSERT_EQUAL(PROCESS_GROUP_USER, getgid());
    TEST_ASSERT_EQUAL(PROCESS_GROUP_USER, getegid());
}

void test_group_iterator_enumerates_builtin_groups_and_resets(void)
{
    setgrent();
    TEST_ASSERT_EQUAL_STRING("root", getgrent()->gr_name);
    TEST_ASSERT_EQUAL_STRING("hund", getgrent()->gr_name);
    TEST_ASSERT_EQUAL_STRING("user", getgrent()->gr_name);
    TEST_ASSERT_NULL(getgrent());

    setgrent();
    TEST_ASSERT_EQUAL_STRING("root", getgrent()->gr_name);
    endgrent();
    TEST_ASSERT_NULL(getgrent());
}

void test_posix_process_id_getters_return_current_and_parent_ids(void)
{
    TEST_ASSERT_EQUAL_INT64(42, getpid());
    TEST_ASSERT_EQUAL_INT64(21, getppid());
}

void test_posix_process_group_and_session_calls_use_kernel_control(void)
{
    TEST_ASSERT_EQUAL_INT64(7, getpgrp());
    TEST_ASSERT_EQUAL(PROCESS_GROUP_GET, last_process_group_request.operation);
    TEST_ASSERT_EQUAL_INT64(0, last_process_group_request.pid);

    TEST_ASSERT_EQUAL_INT(0, setpgid(21, 7));
    TEST_ASSERT_EQUAL(PROCESS_GROUP_SET, last_process_group_request.operation);
    TEST_ASSERT_EQUAL_INT64(21, last_process_group_request.pid);
    TEST_ASSERT_EQUAL_INT64(7, last_process_group_request.process_group_id);

    TEST_ASSERT_EQUAL_INT64(42, setsid());
    TEST_ASSERT_EQUAL(PROCESS_GROUP_CREATE_SESSION, last_process_group_request.operation);
}

void test_posix_process_group_calls_map_kernel_errors(void)
{
    process_group_status = PROCESS_GROUP_PERMISSION_DENIED;
    TEST_ASSERT_EQUAL_INT(-1, setpgid(0, 0));
    TEST_ASSERT_EQUAL_INT(EPERM, errno);

    process_group_status = PROCESS_GROUP_PROCESS_NOT_FOUND;
    TEST_ASSERT_EQUAL_INT64(-1, getpgid(99));
    TEST_ASSERT_EQUAL_INT(ESRCH, errno);
}

void test_posix_getppid_reports_missing_current_process(void)
{
    identity_status = PROCESS_IDENTITY_INVALID_ARGUMENT;

    TEST_ASSERT_EQUAL_INT64(-1, getppid());
    TEST_ASSERT_EQUAL_INT(ESRCH, errno);
}

void test_posix_identity_getters_fail_closed_when_current_process_is_missing(void)
{
    credential_status = PROCESS_CREDENTIALS_NOT_FOUND;

    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, getuid());
    TEST_ASSERT_EQUAL_INT(ESRCH, errno);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, getgid());
    TEST_ASSERT_EQUAL_INT(ESRCH, errno);
}

void test_posix_identity_setters_accept_only_unchanged_identity(void)
{
    TEST_ASSERT_EQUAL_INT(0, setuid(PROCESS_USER_HUND));
    TEST_ASSERT_EQUAL_INT(0, setgid(PROCESS_GROUP_USER));

    TEST_ASSERT_EQUAL_INT(-1, setuid(PROCESS_USER_ROOT));
    TEST_ASSERT_EQUAL_INT(EPERM, errno);
    TEST_ASSERT_EQUAL_INT(-1, setgid(PROCESS_GROUP_ROOT));
    TEST_ASSERT_EQUAL_INT(EPERM, errno);
}

void test_posix_identity_setters_report_missing_current_process(void)
{
    credential_status = PROCESS_CREDENTIALS_NOT_FOUND;

    TEST_ASSERT_EQUAL_INT(-1, setuid(PROCESS_USER_HUND));
    TEST_ASSERT_EQUAL_INT(ESRCH, errno);
    TEST_ASSERT_EQUAL_INT(-1, setgid(PROCESS_GROUP_USER));
    TEST_ASSERT_EQUAL_INT(ESRCH, errno);
}

void test_posix_getpwuid_returns_complete_builtin_account_record(void)
{
    struct passwd * account = getpwuid(PROCESS_USER_USER);

    TEST_ASSERT_EQUAL_STRING("user", account->pw_name);
    TEST_ASSERT_EQUAL(PROCESS_USER_USER, account->pw_uid);
    TEST_ASSERT_EQUAL(PROCESS_GROUP_USER, account->pw_gid);
    TEST_ASSERT_EQUAL_STRING("/", account->pw_dir);
    TEST_ASSERT_EQUAL_STRING("/usr/local/bin/bash", account->pw_shell);
}

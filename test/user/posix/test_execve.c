#include "unity.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "syscalls/calls.h"
#include "syscalls/spawn.h"

static SystemCall last_call;
static SpawnMode last_mode;
static char last_path[PATH_MAX];
static char last_argument[PATH_MAX];
static char last_environment[PATH_MAX];
static char last_current_directory[PATH_MAX];
static SpawnResult syscall_result;
static unsigned syscall_calls;
static unsigned exec_prepare_calls;
static unsigned exec_commit_calls;
static unsigned exec_abort_calls;
static IOStatus exec_commit_status;

int PosixPath__absolute(const char * path, BStringPtr result)
{
    *result = BString__new(path);
    return 0;
}

char * getcwd(char * buffer, size_t size)
{
    strlcpy(buffer, "/work/project", size);
    return buffer;
}

#undef PATH_MAX
#define PATH_MAX 256
#define syscall fake_syscall
#include "../../../user/lib/newlib/environment.c"
#include "../../../user/lib/newlib/exec_payload.c"

ExecPayloadPtr PosixIO__prepare_exec_payload(const char * path, const char * current_directory,
                                             char * const arguments[], char * const environment[])
{
    exec_prepare_calls++;
    return PosixExecPayload__new(path, current_directory, arguments, environment, NULL, 0, NULL, 0);
}

IOStatus PosixIO__commit_exec(void)
{
    exec_commit_calls++;
    return exec_commit_status;
}

void PosixIO__abort_exec(void)
{
    exec_abort_calls++;
}

#include "../../../user/lib/newlib/execve.c"
#undef syscall

SystemCallResult fake_syscall(SystemCall number, Anything arg1, Anything arg2, Anything arg3, Anything arg4,
                              Anything arg5)
{
    last_call = number;
    syscall_calls++;
    ConstSpawnRequestPtr request = (ConstSpawnRequestPtr)arg1;
    last_mode = request->mode;
    ConstExecPayloadPtr payload = (ConstExecPayloadPtr)request->overlay_payload;
    strlcpy(last_path, ExecPayload__path(payload), sizeof(last_path));
    strlcpy(last_current_directory, ExecPayload__current_directory(payload), sizeof(last_current_directory));
    const char * argument = ExecPayload__argument_at(payload, 1);
    if (argument != NULL)
    {
        strlcpy(last_argument, argument, sizeof(last_argument));
    }
    const char * environment = ExecPayload__environment_at(payload, 0);
    if (environment != NULL)
    {
        strlcpy(last_environment, environment, sizeof(last_environment));
    }
    return syscall_result;
}

void setUp(void)
{
    last_call = PRINT;
    last_mode = 0;
    last_path[0] = '\0';
    last_argument[0] = '\0';
    last_environment[0] = '\0';
    last_current_directory[0] = '\0';
    syscall_result = SPAWN_FAILED;
    syscall_calls = 0;
    exec_prepare_calls = 0;
    exec_commit_calls = 0;
    exec_abort_calls = 0;
    exec_commit_status = IO_OK;
    errno = 0;
}

void tearDown(void)
{
}

void test_execve_submits_an_overlay_on_the_existing_exec_syscall(void)
{
    char * arguments[] = {"tool", "argument", NULL};
    char * environment[] = {"PATH=/bin", NULL};

    TEST_ASSERT_EQUAL_INT(-1, execve("/bin/tool", arguments, environment));
    TEST_ASSERT_EQUAL(SYSCALL_EXEC_REQUEST, last_call);
    TEST_ASSERT_EQUAL(P_OVERLAY, last_mode);
    TEST_ASSERT_EQUAL_STRING("/bin/tool", last_path);
    TEST_ASSERT_EQUAL_STRING("argument", last_argument);
    TEST_ASSERT_EQUAL_STRING("PATH=/bin", last_environment);
    TEST_ASSERT_EQUAL_STRING("/work/project", last_current_directory);
    TEST_ASSERT_EQUAL_UINT(1, exec_prepare_calls);
    TEST_ASSERT_EQUAL_UINT(1, exec_commit_calls);
    TEST_ASSERT_EQUAL_UINT(1, exec_abort_calls);
    TEST_ASSERT_EQUAL_INT(ENOEXEC, errno);
}

void test_execve_aborts_prepared_inheritance_when_commit_fails(void)
{
    char * arguments[] = {"tool", NULL};
    exec_commit_status = IO_PERMISSION_DENIED;

    TEST_ASSERT_EQUAL_INT(-1, execve("/bin/tool", arguments, NULL));
    TEST_ASSERT_EQUAL(PRINT, last_call);
    TEST_ASSERT_EQUAL_UINT(1, exec_prepare_calls);
    TEST_ASSERT_EQUAL_UINT(1, exec_commit_calls);
    TEST_ASSERT_EQUAL_UINT(1, exec_abort_calls);
    TEST_ASSERT_EQUAL_INT(EIO, errno);
}

void test_execve_maps_missing_executable_to_enoent(void)
{
    char * arguments[] = {"missing", NULL};
    syscall_result = SPAWN_NO_FILE;

    TEST_ASSERT_EQUAL_INT(-1, execve("/bin/missing", arguments, NULL));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

void test_execve_rejects_an_executable_path_that_cannot_fit_the_payload(void)
{
    char path[PATH_MAX + 1];
    memset(path, 'x', PATH_MAX);
    path[0] = '/';
    path[PATH_MAX] = '\0';

    TEST_ASSERT_EQUAL_INT(-1, execve(path, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(ENAMETOOLONG, errno);
    TEST_ASSERT_EQUAL(PRINT, last_call);
}

void test_execvp_uses_direct_path_without_searching_path(void)
{
    char * arguments[] = {"tool", NULL};

    TEST_ASSERT_EQUAL_INT(-1, execvp("/custom/tool", arguments));
    TEST_ASSERT_EQUAL_STRING("/custom/tool", last_path);
    TEST_ASSERT_EQUAL_UINT(1, syscall_calls);
}

void test_execvp_searches_environment_path_until_a_command_is_found(void)
{
    char path[] = "PATH=/missing:/usr/local/bin";
    char * environment[] = {path, NULL};
    LibC__set_environment(environment);
    char * arguments[] = {"env", NULL};
    syscall_result = SPAWN_NO_FILE;

    TEST_ASSERT_EQUAL_INT(-1, execvp("env", arguments));
    TEST_ASSERT_EQUAL_STRING("/usr/local/bin/env", last_path);
    TEST_ASSERT_EQUAL_UINT(2, syscall_calls);
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

void test_execvp_uses_rootfs_default_path_when_path_is_unset(void)
{
    LibC__set_environment(NULL);
    char * arguments[] = {"env", NULL};

    TEST_ASSERT_EQUAL_INT(-1, execvp("env", arguments));
    TEST_ASSERT_EQUAL_STRING("/usr/local/bin/env", last_path);
    TEST_ASSERT_EQUAL_UINT(1, syscall_calls);
}

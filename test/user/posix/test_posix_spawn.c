#include <stdint.h>
#include <stddef.h>

#include "unity.h"

#include "../../../common_includes/syscalls/calls.h"
#include "../../../user/lib/posix/spawn.h"

static SystemCall last_call;
static SpawnRequest last_request;
static SystemCallResult next_result;

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    last_call = num;
    if (num == SYSCALL_EXEC || num == SYSCALL_EXEC_REQUEST)
    {
        last_request = *((SpawnRequestPtr)arg1);
    }

    return next_result;
}

#include "../../../user/lib/sys/spawn.c"
#include "../../../user/lib/posix/spawn.c"

void setUp(void)
{
    last_call = 0;
    last_request = (SpawnRequest){0};
    next_result = 0;
}

void tearDown(void)
{
}

static void assert_last_request_argument(StringPos index, const char * expected)
{
    match_optional(
        SpawnArguments__at(&last_request.arguments, index),
        some(argument) { TEST_ASSERT_EQUAL_STRING(expected, BString__cstr(&argument)); },
        none_test_fail()
    );
}

void test_posix_spawnv_converts_argv_after_argv0_to_hundos_spawn_arguments(void)
{
    char * argv[] = {"tool", "one", "two", "three", "four", NULL};
    next_result = 42;

    match_result(
        POSIX__spawnv(P_NOWAIT, "/sbin/tool", argv),
        success(pid)
        {
            TEST_ASSERT_EQUAL_INT64(42, pid);
            TEST_ASSERT_EQUAL(SYSCALL_EXEC, last_call);
            TEST_ASSERT_EQUAL_STRING("/sbin/tool", BString__cstr(&last_request.path));
            TEST_ASSERT_EQUAL_UINT64(4, SpawnArguments__len(&last_request.arguments));
            assert_last_request_argument(0, "one");
            assert_last_request_argument(3, "four");
        },
        error_test_fail()
    );
}

void test_posix_spawnv_rejects_missing_path(void)
{
    char * argv[] = {"tool", NULL};

    match_result(
        POSIX__spawnv(P_NOWAIT, NULL, argv),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT64(SPAWN_NO_PATH, code); }
    );
}

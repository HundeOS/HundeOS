#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/calls.h"
#include "../../../../common_includes/syscalls/spawn.h"

static SystemCall last_call;
static Anything last_arg1;
static SpawnRequest last_request;
static SystemCallResult next_result;

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    last_call = num;
    last_arg1 = arg1;
    if (num == SYSCALL_EXEC || num == SYSCALL_EXEC_REQUEST)
    {
        last_request = *((SpawnRequestPtr) arg1);
    }

    return next_result;
}

#include "../../../../user/lib/sys/spawn.c"

void setUp(void)
{
    last_call = 0;
    last_arg1 = 0;
    last_request = (SpawnRequest) { 0 };
    next_result = 0;
}

void tearDown(void)
{
}

static void assert_last_request_argument(StringPos index, const char * expected)
{
    match_optional(BString, SpawnArguments__at(&last_request.arguments, index),
        some(argument) { TEST_ASSERT_EQUAL_STRING(expected, BString__cstr(&argument)); },
        none_test_fail()
    );
}

void test_os_spawn_returns_success_result_for_spawned_pid(void)
{
    next_result = 17;

    match_result(SpawnResult, OS__spawn(P_NOWAIT, &bstr("/sbin/test"), &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG),
        success(pid)
        {
            TEST_ASSERT_EQUAL_INT64(17, pid);
            TEST_ASSERT_EQUAL(SYSCALL_EXEC, last_call);
            TEST_ASSERT_EQUAL_STRING("/sbin/test", BString__cstr(&last_request.path));
            TEST_ASSERT_EQUAL(P_NOWAIT, last_request.mode);
            TEST_ASSERT_EQUAL_UINT64(0, SpawnArguments__len(&last_request.arguments));
        },
        error_test_fail()
    );
}

void test_os_spawn_copies_legacy_optional_args_into_spawn_arguments(void)
{
    next_result = 18;

    match_result(SpawnResult, OS__spawn(
            P_NOWAIT,
            &bstr("/sbin/test"),
            &Some(BString, bstring("one")),
            &Some(BString, bstring("two")),
            &SPAWN_NO_ARG
        ),
        success(pid)
        {
            TEST_ASSERT_EQUAL_INT64(18, pid);
            TEST_ASSERT_EQUAL_UINT64(2, SpawnArguments__len(&last_request.arguments));
            assert_last_request_argument(0, "one");
            assert_last_request_argument(1, "two");
        },
        error_test_fail()
    );
}

void test_os_spawn_argv_accepts_more_than_three_args(void)
{
    SpawnArguments arguments = SpawnArguments__new();
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("one")), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("two")), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("three")), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, SpawnArguments__push(&arguments, bstring("four")), success_do_nothing(), error_test_fail());
    next_result = 19;

    match_result(SpawnResult, OS__spawn_argv(P_NOWAIT, &bstr("/sbin/test"), &arguments),
        success(pid)
        {
            TEST_ASSERT_EQUAL_INT64(19, pid);
            TEST_ASSERT_EQUAL(SYSCALL_EXEC, last_call);
            TEST_ASSERT_EQUAL_UINT64(4, SpawnArguments__len(&last_request.arguments));
            assert_last_request_argument(0, "one");
            assert_last_request_argument(3, "four");
        },
        error_test_fail()
    );
}

void test_os_spawn_returns_error_result_for_spawn_failure(void)
{
    next_result = SPAWN_NO_FILE;

    match_result(SpawnResult, OS__spawn(P_NOWAIT, &bstr("/sbin/missing"), &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG),
        success(pid)
        {
            TEST_FAIL();
        },
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT64(SPAWN_NO_FILE, code);
            TEST_ASSERT_EQUAL_STRING("spawn failed: file not found", BString__cstr(&message));
        }
    );
}

void test_os_spawn_with_stdio_uses_exec_request_syscall(void)
{
    next_result = 42;

    match_result(SpawnResult, OS__spawn_with_stdio(
            P_NOWAIT,
            &bstr("/sbin/shell"),
            3,
            4,
            5,
            &SPAWN_NO_ARG,
            &SPAWN_NO_ARG,
            &SPAWN_NO_ARG
        ),
        success(pid)
        {
            TEST_ASSERT_EQUAL_INT64(42, pid);
            TEST_ASSERT_EQUAL(SYSCALL_EXEC_REQUEST, last_call);
            TEST_ASSERT_TRUE(SpawnStdio__is_present(last_request.stdio));
            TEST_ASSERT_EQUAL_UINT64(3, SpawnStdio__stdin_fd(last_request.stdio));
            TEST_ASSERT_EQUAL_UINT64(4, SpawnStdio__stdout_fd(last_request.stdio));
            TEST_ASSERT_EQUAL_UINT64(5, SpawnStdio__stderr_fd(last_request.stdio));
        },
        error_test_fail()
    );
}

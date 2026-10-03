#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "unity.h"

#define KILL_TEST_MAX_CALLS 4

typedef struct KillTestCall
{
    pid_t pid;
    int signal;
} KillTestCall;

static KillTestCall calls[KILL_TEST_MAX_CALLS];
static size_t call_count;
static int requested_exit_status;
static jmp_buf exit_context;

static int KillTest__kill(pid_t pid, int signal);
static int KillTest__pledge(const char * promises, const char * paths);
static long long KillTest__strtonum(const char * text, long long minimum, long long maximum, const char ** error);
static void KillTest__exit(int status) __attribute__((noreturn));

#define exit KillTest__exit
#define kill KillTest__kill
#define main kill_main_under_test
#define pledge KillTest__pledge
#define strtonum KillTest__strtonum
#include "../../../../vendor/openbsd/src/bin/kill/kill.c"
#undef exit
#undef kill
#undef main
#undef pledge
#undef strtonum

char * __progname = "kill";

void setUp(void)
{
    call_count = 0;
    requested_exit_status = -1;
}

void tearDown(void)
{
}

static int KillTest__kill(pid_t pid, int signal)
{
    TEST_ASSERT_LESS_THAN_UINT64(KILL_TEST_MAX_CALLS, call_count);
    calls[call_count++] = (KillTestCall){.pid = pid, .signal = signal};
    return 0;
}

static int KillTest__pledge(const char * promises, const char * paths)
{
    return 0;
}

static long long KillTest__strtonum(const char * text, long long minimum, long long maximum, const char ** error)
{
    char * end;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (*text == '\0' || *end != '\0')
    {
        *error = "invalid";
        return 0;
    }
    if (errno == ERANGE || value < minimum || value > maximum)
    {
        errno = ERANGE;
        *error = value < minimum ? "too small" : "too large";
        return 0;
    }
    *error = NULL;
    return value;
}

static void KillTest__exit(int status)
{
    requested_exit_status = status;
    longjmp(exit_context, 1);
}

static int KillTest__run(int argc, char ** argv)
{
    if (setjmp(exit_context) == 0)
    {
        kill_main_under_test(argc, argv);
        TEST_FAIL_MESSAGE("OpenBSD kill returned without calling exit");
    }
    return requested_exit_status;
}

void test_kill_defaults_to_terminate_signal(void)
{
    char * argv[] = {"kill", "42"};

    TEST_ASSERT_EQUAL_INT(0, KillTest__run(2, argv));
    TEST_ASSERT_EQUAL_UINT64(1, call_count);
    TEST_ASSERT_EQUAL_INT(42, calls[0].pid);
    TEST_ASSERT_EQUAL_INT(SIGTERM, calls[0].signal);
}

void test_kill_accepts_named_signal(void)
{
    char * argv[] = {"kill", "-KILL", "77"};

    TEST_ASSERT_EQUAL_INT(0, KillTest__run(3, argv));
    TEST_ASSERT_EQUAL_UINT64(1, call_count);
    TEST_ASSERT_EQUAL_INT(77, calls[0].pid);
    TEST_ASSERT_EQUAL_INT(SIGKILL, calls[0].signal);
}

void test_kill_accepts_numeric_signal(void)
{
    char * argv[] = {"kill", "-2", "9"};

    TEST_ASSERT_EQUAL_INT(0, KillTest__run(3, argv));
    TEST_ASSERT_EQUAL_UINT64(1, call_count);
    TEST_ASSERT_EQUAL_INT(9, calls[0].pid);
    TEST_ASSERT_EQUAL_INT(SIGINT, calls[0].signal);
}

void test_kill_accepts_signal_zero_and_process_group(void)
{
    char * argv[] = {"kill", "-s", "0", "-123"};

    TEST_ASSERT_EQUAL_INT(0, KillTest__run(4, argv));
    TEST_ASSERT_EQUAL_UINT64(1, call_count);
    TEST_ASSERT_EQUAL_INT(-123, calls[0].pid);
    TEST_ASSERT_EQUAL_INT(0, calls[0].signal);
}

void test_kill_sends_to_each_target(void)
{
    char * argv[] = {"kill", "12", "34"};

    TEST_ASSERT_EQUAL_INT(0, KillTest__run(3, argv));
    TEST_ASSERT_EQUAL_UINT64(2, call_count);
    TEST_ASSERT_EQUAL_INT(12, calls[0].pid);
    TEST_ASSERT_EQUAL_INT(34, calls[1].pid);
}

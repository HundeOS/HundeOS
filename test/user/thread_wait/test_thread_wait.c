#include "unity.h"
#include "sys/thread_wait.h"
#include "sys/user_mutex.h"
#include "sys/syscall.h"

static unsigned wait_calls;
static unsigned wake_calls;
static Anything requested_word;
static Anything requested_expected;
static Anything requested_timeout;
static Anything requested_count;
static ThreadWaitStatus wait_status;
static ThreadWakeResult wake_result;
static bool release_on_wait;
static bool interrupt_first_wait;

SystemCallResult syscall(SystemCall call, Anything first, Anything second, Anything third, Anything fourth,
                         Anything fifth)
{
    TEST_ASSERT_EQUAL(0, fourth);
    TEST_ASSERT_EQUAL(0, fifth);
    requested_word = first;
    if (call == SYSCALL_THREAD_WAIT)
    {
        wait_calls++;
        requested_expected = second;
        requested_timeout = third;
        if (interrupt_first_wait && wait_calls == 1)
        {
            return THREAD_WAIT_INTERRUPTED;
        }
        if (release_on_wait)
        {
            atomic_store_explicit((ThreadWaitWordPtr)first, 0, memory_order_release);
        }
        return wait_status;
    }
    TEST_ASSERT_EQUAL(SYSCALL_THREAD_WAKE, call);
    TEST_ASSERT_EQUAL(0, third);
    wake_calls++;
    requested_count = second;
    return wake_result;
}

void setUp(void)
{
    wait_calls = 0;
    wake_calls = 0;
    wait_status = THREAD_WAIT_WOKEN;
    wake_result = 1;
    release_on_wait = false;
    interrupt_first_wait = false;
}

void tearDown(void)
{
}

void test_wait_and_wake_wrappers_preserve_arguments_and_results(void)
{
    ThreadWaitWord word = 7;
    wait_status = THREAD_WAIT_TIMED_OUT;
    TEST_ASSERT_EQUAL(THREAD_WAIT_TIMED_OUT, thread_wait(&word, 7, 1234));
    TEST_ASSERT_EQUAL_HEX64((Anything)&word, requested_word);
    TEST_ASSERT_EQUAL(7, requested_expected);
    TEST_ASSERT_EQUAL(1234, requested_timeout);
    wake_result = 3;
    TEST_ASSERT_EQUAL(3, thread_wake(&word, 4));
    TEST_ASSERT_EQUAL_HEX64((Anything)&word, requested_word);
    TEST_ASSERT_EQUAL(4, requested_count);
}

void test_uncontended_mutex_does_not_make_syscalls(void)
{
    UserMutex mutex = UserMutex__new();
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__lock(&mutex));
    TEST_ASSERT_FALSE(UserMutex__try_lock(&mutex));
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__unlock(&mutex));
    TEST_ASSERT_EQUAL(0, wait_calls);
    TEST_ASSERT_EQUAL(0, wake_calls);
    TEST_ASSERT_TRUE(UserMutex__try_lock(&mutex));
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__unlock(&mutex));
}

void test_contended_mutex_parks_then_wakes_one_successor(void)
{
    UserMutex mutex = UserMutex__new();
    TEST_ASSERT_TRUE(UserMutex__try_lock(&mutex));
    release_on_wait = true;
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__lock(&mutex));
    TEST_ASSERT_EQUAL(1, wait_calls);
    TEST_ASSERT_EQUAL(2, requested_expected);
    TEST_ASSERT_EQUAL_UINT64(THREAD_WAIT_INDEFINITE, requested_timeout);
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__unlock(&mutex));
    TEST_ASSERT_EQUAL(1, wake_calls);
    TEST_ASSERT_EQUAL(1, requested_count);
}

void test_mutex_retries_interruption_and_changed_value(void)
{
    UserMutex mutex = UserMutex__new();
    TEST_ASSERT_TRUE(UserMutex__try_lock(&mutex));
    release_on_wait = true;
    interrupt_first_wait = true;
    wait_status = THREAD_WAIT_CHANGED;
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__lock(&mutex));
    TEST_ASSERT_EQUAL(2, wait_calls);
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, UserMutex__unlock(&mutex));
}

void test_mutex_propagates_wait_and_wake_failures(void)
{
    UserMutex mutex = UserMutex__new();
    TEST_ASSERT_TRUE(UserMutex__try_lock(&mutex));
    wait_status = THREAD_WAIT_INVALID;
    TEST_ASSERT_EQUAL(THREAD_WAIT_INVALID, UserMutex__lock(&mutex));
    wake_result = THREAD_WAIT_INVALID;
    TEST_ASSERT_EQUAL(THREAD_WAIT_INVALID, UserMutex__unlock(&mutex));
}

void test_unlocking_unlocked_mutex_is_rejected(void)
{
    UserMutex mutex = UserMutex__new();
    TEST_ASSERT_EQUAL(THREAD_WAIT_INVALID, UserMutex__unlock(&mutex));
    TEST_ASSERT_EQUAL(0, wake_calls);
}

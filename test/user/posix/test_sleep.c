#include "unity.h"

#include "../../../common_includes/syscalls/process.h"

static unsigned int sleep_calls;
static uint64_t requested_usec;
static ProcessSleepResult sleep_result;
static ProcessSignalResult signal_control_result;
static ProcessSignalControlRequest signal_request;
static uint64_t previous_alarm_seconds;

ProcessSleepResult sleep_usec(uint64_t usec)
{
    sleep_calls++;
    requested_usec = usec;
    return sleep_result;
}

ProcessSignalResult process_signal_control(ProcessSignalControlRequestPtr request)
{
    signal_request = *request;
    if (signal_control_result == PROCESS_SIGNAL_OK)
    {
        request->previous_alarm_seconds = previous_alarm_seconds;
    }
    return signal_control_result;
}

#include "../../../user/lib/newlib/sleep.c"

void setUp(void)
{
    sleep_calls = 0;
    requested_usec = 0;
    sleep_result = PROCESS_SLEEP_OK;
    signal_control_result = PROCESS_SIGNAL_OK;
    signal_request = (ProcessSignalControlRequest){0};
    previous_alarm_seconds = 0;
}

void tearDown(void)
{
}

void test_sleep_converts_seconds_to_microseconds(void)
{
    TEST_ASSERT_EQUAL_UINT(0, sleep(3));
    TEST_ASSERT_EQUAL_UINT(1, sleep_calls);
    TEST_ASSERT_EQUAL_UINT64(3000000, requested_usec);
}

void test_sleep_zero_returns_without_native_sleep(void)
{
    TEST_ASSERT_EQUAL_UINT(0, sleep(0));
    TEST_ASSERT_EQUAL_UINT(0, sleep_calls);
}

void test_sleep_returns_zero_when_native_sleep_fails(void)
{
    sleep_result = PROCESS_SLEEP_FAILED;

    TEST_ASSERT_EQUAL_UINT(0, sleep(7));
    TEST_ASSERT_EQUAL_UINT(1, sleep_calls);
    TEST_ASSERT_EQUAL_UINT64(7000000, requested_usec);
}

void test_alarm_schedules_signal_timer_and_returns_previous_seconds(void)
{
    previous_alarm_seconds = 12;

    TEST_ASSERT_EQUAL_UINT(12, alarm(30));
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_CONTROL_ALARM, signal_request.operation);
    TEST_ASSERT_EQUAL_UINT64(30, signal_request.alarm_seconds);
    TEST_ASSERT_EQUAL_UINT(0, sleep_calls);
}

void test_alarm_zero_cancels_signal_timer(void)
{
    TEST_ASSERT_EQUAL_UINT(0, alarm(0));
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_CONTROL_ALARM, signal_request.operation);
    TEST_ASSERT_EQUAL_UINT64(0, signal_request.alarm_seconds);
    TEST_ASSERT_EQUAL_UINT(0, sleep_calls);
}

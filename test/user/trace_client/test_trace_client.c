#include "unity.h"

#include "sys/syslog.h"
#include "trace/client.h"

static BString captured_tag;
static BString captured_message;
static SyslogResult syslog_result;

SyslogResult kernel_syslog_tag(ConstBStringPtr tag, ConstBStringPtr message)
{
    captured_tag = *tag;
    captured_message = *message;
    return syslog_result;
}

void setUp(void)
{
    captured_tag = empty_bstr();
    captured_message = empty_bstr();
    syslog_result = SYSLOG_OK;
}

void tearDown(void)
{
}

void test_driver_trace_writes_directly_to_kernel_syslog(void)
{
    TEST_ASSERT_EQUAL(TRACE_OK, DriverTrace__write(&bstr("sdhci"), &bstr("ready\n")));
    TEST_ASSERT_EQUAL_BSTRING(bstr("sdhci"), captured_tag);
    TEST_ASSERT_EQUAL_BSTRING(bstr("ready\n"), captured_message);
}

void test_driver_trace_formats_message_before_kernel_syslog(void)
{
    TEST_ASSERT_EQUAL(TRACE_OK, DriverTrace__writef("uart", "irq=%i\n", 44));
    TEST_ASSERT_EQUAL_BSTRING(bstr("uart"), captured_tag);
    TEST_ASSERT_EQUAL_BSTRING(bstr("irq=44\n"), captured_message);
}

void test_driver_trace_reports_kernel_syslog_failure(void)
{
    syslog_result = SYSLOG_INVALID_ARGUMENT;

    TEST_ASSERT_EQUAL(TRACE_UNAVAILABLE, DriverTrace__write(&bstr("sdhci"), &bstr("failed\n")));
}

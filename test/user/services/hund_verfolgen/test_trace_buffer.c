#include "../../../../user/services/hund-verfolgen/trace_buffer.h"

#include "unity.h"

static BString full_bstring(void)
{
    BString result = empty_bstr();
    while (BString__push_char(&result, 'x'))
    {
    }
    return result;
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_trace_buffer_appends_records_and_normalizes_newlines(void)
{
    TraceBuffer buffer = TraceBuffer__new();
    BString first = bstr("first");
    BString second = bstr("second\n");

    TEST_ASSERT_TRUE(TraceBuffer__append(&buffer, &first));
    TEST_ASSERT_TRUE(TraceBuffer__append(&buffer, &second));

    static const char expected[] = "first\nsecond\n";
    TEST_ASSERT_EQUAL_UINT64(sizeof(expected) - 1, TraceBuffer__length(&buffer));
    TEST_ASSERT_EQUAL_MEMORY(expected, TraceBuffer__data(&buffer), sizeof(expected) - 1);
    TEST_ASSERT_EQUAL_UINT64(0, TraceBuffer__dropped(&buffer));
}

void test_trace_buffer_counts_records_dropped_at_capacity(void)
{
    TraceBuffer buffer = TraceBuffer__new();
    BString full = full_bstring();

    while (TraceBuffer__append(&buffer, &full))
    {
    }

    TEST_ASSERT_EQUAL_UINT64(1, TraceBuffer__dropped(&buffer));
    TEST_ASSERT_TRUE(TraceBuffer__length(&buffer) <= TRACE_BUFFER_CAPACITY);
}

void test_trace_buffer_clear_returns_and_resets_drop_count(void)
{
    TraceBuffer buffer = TraceBuffer__new();
    BString full = full_bstring();

    while (TraceBuffer__append(&buffer, &full))
    {
    }

    TEST_ASSERT_EQUAL_UINT64(1, TraceBuffer__clear(&buffer));
    TEST_ASSERT_TRUE(TraceBuffer__is_empty(&buffer));
    TEST_ASSERT_EQUAL_UINT64(0, TraceBuffer__dropped(&buffer));
}

void test_trace_buffer_consumes_only_written_prefix(void)
{
    TraceBuffer buffer = TraceBuffer__new();
    BString line = bstr("first\nsecond");

    TEST_ASSERT_TRUE(TraceBuffer__append(&buffer, &line));
    TEST_ASSERT_TRUE(TraceBuffer__consume(&buffer, 6));

    static const char expected[] = "second\n";
    TEST_ASSERT_EQUAL_UINT64(sizeof(expected) - 1, TraceBuffer__length(&buffer));
    TEST_ASSERT_EQUAL_MEMORY(expected, TraceBuffer__data(&buffer), sizeof(expected) - 1);
    TEST_ASSERT_FALSE(TraceBuffer__consume(&buffer, sizeof(expected)));
}

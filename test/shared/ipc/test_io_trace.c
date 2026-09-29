#include "unity.h"
#include "ipc/io_trace.h"
#include "ipc/trace_protocol.h"

static IOTraceRing ring;

void setUp(void)
{
    IOTraceRing__initialize(&ring);
}

void tearDown(void)
{
}

static IOTraceEvent event(uint64_t generation, uint64_t timestamp)
{
    RequestTicket ticket = RequestTicket__new(17, generation, 42, &None(RequestTicket));
    return IOTraceEvent__new(IO_TRACE_CLIENT_PUBLISH, &ticket, timestamp);
}

void test_io_trace_ring_publishes_stable_events(void)
{
    IOTraceEvent first = event(1, 100);
    IOTraceRing__publish(&ring, &first);

    TEST_ASSERT_TRUE(IOTraceRing__ready(&ring));
    TEST_ASSERT_EQUAL_UINT64(1, IOTraceRing__produced(&ring));
    match_optional(
        IOTraceRing__read(&ring, 0),
        some(observed)
        {
            TEST_ASSERT_EQUAL_UINT64(100, observed.timestamp_usec);
            TEST_ASSERT_TRUE(RequestTicket__equals(&first.ticket, &observed.ticket));
        },
        none_test_fail()
    );
}

void test_io_trace_ring_reports_oldest_retained_cursor_after_overwrite(void)
{
    for (uint64_t index = 0; index < IO_TRACE_RING_CAPACITY + 3; index++)
    {
        IOTraceEvent next = event(index + 1, index);
        IOTraceRing__publish(&ring, &next);
    }

    uint64_t produced = IOTraceRing__produced(&ring);
    TEST_ASSERT_EQUAL_UINT64(3, IOTraceRing__oldest(&ring, produced));
    TEST_ASSERT_TRUE(is_nothing(IOTraceRing__read(&ring, 0)));
    match_optional(
        IOTraceRing__read(&ring, 3),
        some(observed)
        {
            TEST_ASSERT_EQUAL_UINT64(3, observed.timestamp_usec);
        },
        none_test_fail()
    );
}

void test_trace_protocol_constructs_io_drain_request(void)
{
    TraceRequest request = TraceRequest__drain_io();

    TEST_ASSERT_EQUAL(TRACE_OPERATION_DRAIN_IO, request.operation);
}

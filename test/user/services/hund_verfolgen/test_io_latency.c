#include "../../../../user/services/hund-verfolgen/io_latency.h"

#include "unity.h"

static IOLatencyTracker tracker;
static RequestTicket ticket;

void setUp(void)
{
    tracker = IOLatencyTracker__new();
    RequestTicket parent = RequestTicket__new(11, 4, 42, &None(RequestTicket));
    ticket = RequestTicket__new(17, 8, 42, &Some(RequestTicket, parent));
}

void tearDown(void)
{
}

static Optional(BString) observe(uint64_t pid, IOTraceEventKind kind, uint64_t timestamp)
{
    IOTraceEvent event = {.ticket = ticket, .timestamp_usec = timestamp, .kind = kind};
    return IOLatencyTracker__record(&tracker, pid, &event);
}

void test_io_latency_correlates_out_of_order_process_batches(void)
{
    TEST_ASSERT_TRUE(is_nothing(observe(50, IO_TRACE_SERVER_RECEIVE, 110)));
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_CLIENT_COMPLETE, 145)));
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_CLIENT_PUBLISH, 100)));
    TEST_ASSERT_TRUE(is_nothing(observe(50, IO_TRACE_REQUEST_KERNEL_ELIGIBLE, 102)));
    TEST_ASSERT_TRUE(is_nothing(observe(50, IO_TRACE_REQUEST_KERNEL_SELECTED, 104)));
    TEST_ASSERT_TRUE(is_nothing(observe(50, IO_TRACE_REQUEST_KERNEL_DISPATCHED, 106)));
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_RETURN_KERNEL_ELIGIBLE, 132)));
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_RETURN_KERNEL_SELECTED, 135)));
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_RETURN_KERNEL_DISPATCHED, 140)));
    match_optional(
        observe(50, IO_TRACE_SERVER_REPLY, 130),
        some(summary)
        {
            TEST_ASSERT_TRUE(BString__contains_char(&summary, '='));
            TEST_ASSERT_TRUE(BString__starts_with(&summary, &bstr("client=42 server=50")));
            TEST_ASSERT_TRUE(BString__equals(
                &summary, &bstr("client=42 server=50 connection=17 generation=8 origin=42 parent_connection=11 "
                                "parent_generation=4 queue_usec=10 published_at_usec=100 received_at_usec=110 "
                                "replied_at_usec=130 completed_at_usec=145 request_scheduler_observed=1 "
                                "request_observe_usec=2 request_runnable_usec=2 request_dispatch_usec=2 "
                                "request_userspace_usec=4 service_usec=20 return_usec=15 "
                                "return_scheduler_observed=1 return_observe_usec=2 return_runnable_usec=3 "
                                "return_dispatch_usec=5 return_userspace_usec=5 total_usec=45")));
        },
        none_test_fail()
    );
}

void test_io_latency_reports_completed_channel_without_scheduler_observation(void)
{
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_CLIENT_PUBLISH, 100)));
    TEST_ASSERT_TRUE(is_nothing(observe(50, IO_TRACE_SERVER_RECEIVE, 110)));
    TEST_ASSERT_TRUE(is_nothing(observe(50, IO_TRACE_SERVER_REPLY, 130)));
    match_optional(
        observe(42, IO_TRACE_CLIENT_COMPLETE, 145),
        some(summary)
        {
            TEST_ASSERT_TRUE(BString__equals(
                &summary, &bstr("client=42 server=50 connection=17 generation=8 origin=42 parent_connection=11 "
                                "parent_generation=4 queue_usec=10 published_at_usec=100 received_at_usec=110 "
                                "replied_at_usec=130 completed_at_usec=145 request_scheduler_observed=0 "
                                "request_observe_usec=0 request_runnable_usec=0 request_dispatch_usec=0 "
                                "request_userspace_usec=0 service_usec=20 return_usec=15 "
                                "return_scheduler_observed=0 return_observe_usec=0 return_runnable_usec=0 "
                                "return_dispatch_usec=0 return_userspace_usec=0 total_usec=45")));
        },
        none_test_fail()
    );
}

void test_io_latency_rejects_non_monotonic_timestamps(void)
{
    observe(42, IO_TRACE_CLIENT_PUBLISH, 100);
    observe(50, IO_TRACE_REQUEST_KERNEL_ELIGIBLE, 102);
    observe(50, IO_TRACE_REQUEST_KERNEL_SELECTED, 104);
    observe(50, IO_TRACE_REQUEST_KERNEL_DISPATCHED, 106);
    observe(50, IO_TRACE_SERVER_RECEIVE, 90);
    observe(50, IO_TRACE_SERVER_REPLY, 130);
    observe(42, IO_TRACE_RETURN_KERNEL_ELIGIBLE, 132);
    observe(42, IO_TRACE_RETURN_KERNEL_SELECTED, 135);
    observe(42, IO_TRACE_RETURN_KERNEL_DISPATCHED, 140);
    TEST_ASSERT_TRUE(is_nothing(observe(42, IO_TRACE_CLIENT_COMPLETE, 145)));
}

void test_io_latency_reports_publish_to_consumption_hop_from_server_event(void)
{
    IOTraceEvent event = IOTraceEvent__new_with_peer(IO_TRACE_SERVER_RECEIVE, &ticket, 145, 42, 100);

    match_optional(
        IOLatencyTracker__hop(50, &event),
        some(summary)
        {
            TEST_ASSERT_EQUAL_BSTRING(
                bstr("client=42 server=50 connection=17 generation=8 origin=42 parent_connection=11 "
                     "parent_generation=4 published_at_usec=100 consumed_at_usec=145 queue_usec=45"),
                summary);
        },
        none_test_fail()
    );
}

void test_io_latency_rejects_invalid_publish_to_consumption_hop(void)
{
    IOTraceEvent missing = IOTraceEvent__new(IO_TRACE_SERVER_RECEIVE, &ticket, 145);
    TEST_ASSERT_TRUE(is_nothing(IOLatencyTracker__hop(50, &missing)));

    IOTraceEvent reversed = IOTraceEvent__new_with_peer(IO_TRACE_SERVER_RECEIVE, &ticket, 99, 42, 100);
    TEST_ASSERT_TRUE(is_nothing(IOLatencyTracker__hop(50, &reversed)));
}

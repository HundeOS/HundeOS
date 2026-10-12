#include "unity.h"

#include "../../../user/lib/trace/io_trace.c"

static IOTraceRing trace_ring;
static unsigned channel_opens;
static unsigned channel_submissions;
static bool io_latency_tracing_enabled;

Result(SystemSettings) SystemSettings__load(void)
{
    return Success(SystemSettings, SystemSettings__new(io_latency_tracing_enabled));
}

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr target_service)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(HUND_VERFOLGEN_SERVICE), *target_service);
    channel_opens++;
    return Success(Channel, ((Channel){.server_pid = 42}));
}

Result(ChannelReplySize) Channel__submit(ChannelPtr self, ConstChannelDataPtr request, size_t request_size,
                                         ChannelDataPtr reply, size_t reply_capacity)
{
    TEST_ASSERT_EQUAL_UINT64(sizeof(TraceRequest), request_size);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(sizeof(TraceReply), reply_capacity);
    TEST_ASSERT_EQUAL(TRACE_OPERATION_REGISTER_IO, ((ConstTraceRequestPtr)request)->operation);
    TraceReply response = TraceReply__new(TRACE_OK);
    response.buffer = (BufferDescriptor){
        .id = BUFFER_LIMIT,
        .address = (Address)&trace_ring,
        .capacity = BUFFER_SIZE,
        .direction = BUFFER_WRITE,
    };
    __builtin_memcpy(reply, &response, sizeof(response));
    channel_submissions++;
    return Success(ChannelReplySize, sizeof(response));
}

ChannelCode Channel__close(ChannelPtr self)
{
    return CHANNEL_OK;
}

bool BufferClient__validate(ConstBufferDescriptorPtr buffer, BufferParticipant peer, BufferDirection direction)
{
    return buffer->address == (Address)&trace_ring && peer == 42 && direction == BUFFER_WRITE;
}

Optional(QueueDispatchTrace) QueueWait__take_dispatch_trace(ConstRequestTicketPtr ticket)
{
    return None(QueueDispatchTrace);
}

void setUp(void)
{
    __builtin_memset(&trace_ring, 0, sizeof(trace_ring));
    channel_opens = 0;
    channel_submissions = 0;
    io_latency_tracing_enabled = true;
    IOTrace__set_enabled(true);
    IOTrace__reset_after_fork();
}

void test_disabled_system_setting_skips_registration(void)
{
    io_latency_tracing_enabled = false;

    TEST_ASSERT_EQUAL(TRACE_OK, IOTrace__prepare());
    TEST_ASSERT_EQUAL_UINT64(0, channel_opens);
    TEST_ASSERT_EQUAL_UINT64(0, channel_submissions);
}

void tearDown(void)
{
}

static RequestTicket valid_ticket(void)
{
    return (RequestTicket){.connection = 7, .metadata = {.generation = 3, .origin_pid = 11}};
}

void test_record_without_preparation_does_not_open_a_channel(void)
{
    RequestTicket ticket = valid_ticket();

    IOTrace__record(IO_TRACE_CLIENT_PUBLISH, &ticket, 100);
    IOTrace__record_with_peer(IO_TRACE_SERVER_RECEIVE, &ticket, 110, 42, 90);

    TEST_ASSERT_EQUAL_UINT64(0, channel_opens);
    TEST_ASSERT_EQUAL_UINT64(0, channel_submissions);
    TEST_ASSERT_EQUAL_UINT64(0, IOTraceRing__produced(&trace_ring));
}

void test_preparation_registers_once_and_records_to_the_shared_ring(void)
{
    RequestTicket ticket = valid_ticket();

    TEST_ASSERT_EQUAL(TRACE_OK, IOTrace__prepare());
    TEST_ASSERT_EQUAL_UINT64(1, channel_opens);
    TEST_ASSERT_EQUAL_UINT64(1, channel_submissions);

    IOTrace__record(IO_TRACE_CLIENT_PUBLISH, &ticket, 100);
    IOTrace__record_with_peer(IO_TRACE_SERVER_RECEIVE, &ticket, 110, 42, 90);

    TEST_ASSERT_EQUAL_UINT64(1, channel_opens);
    TEST_ASSERT_EQUAL_UINT64(1, channel_submissions);
    TEST_ASSERT_EQUAL_UINT64(2, IOTraceRing__produced(&trace_ring));
    match_optional(
        IOTraceRing__read(&trace_ring, 0),
        some(event)
        {
            TEST_ASSERT_EQUAL(IO_TRACE_CLIENT_PUBLISH, event.kind);
            TEST_ASSERT_EQUAL_UINT64(100, event.timestamp_usec);
        },
        none_test_fail()
    );
    match_optional(
        IOTraceRing__read(&trace_ring, 1),
        some(event)
        {
            TEST_ASSERT_EQUAL(IO_TRACE_SERVER_RECEIVE, event.kind);
            TEST_ASSERT_EQUAL_UINT64(42, event.peer_pid);
            TEST_ASSERT_EQUAL_UINT64(90, event.peer_timestamp_usec);
        },
        none_test_fail()
    );
}

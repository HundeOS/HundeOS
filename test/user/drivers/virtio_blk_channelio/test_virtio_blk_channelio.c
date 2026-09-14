#include "unity.h"

#define Channel__open fake_channel_open
#define Channel__request fake_channel_request
#define Channel__poll_reply fake_channel_poll_reply
#define Channel__submit fake_channel_submit
#define Channel__prepare_wait fake_channel_prepare_wait
#define Channel__wait_event fake_channel_wait_event
#define Channel__close fake_channel_close
#include "../../../../user/drivers/virtio_blk_channelio/channelio.c"

static InterruptProtocolResponse pending_response;
static bool pending_response_present;
static InterruptProtocolRequest last_request;
static bool request_sent;
static unsigned reply_polls;

void setUp(void)
{
    interrupt_channel_storage = (Channel){0};
    interrupt_channel = Some(ChannelPtr, &interrupt_channel_storage);
    subscribed_irq = 48;
    registration_generation = 7;
    delivery_sequence = 0;
    pending_response = (InterruptProtocolResponse){0};
    pending_response_present = false;
    last_request = (InterruptProtocolRequest){0};
    request_sent = false;
    reply_polls = 0;
}

void tearDown(void)
{
}

ServiceCode service_poll(ServiceEventPtr event)
{
    return SERVICE_EVENT_QUEUE_EMPTY;
}

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}

Result(Channel) fake_channel_open(ServiceDirectoryPtr directory, ConstBStringPtr target_service)
{
    return Error(Channel, bstr("not used"), CHANNEL_UNAVAILABLE);
}

Result(ChannelCode) fake_channel_request(ChannelPtr self, ConstChannelDataPtr request, size_t request_size)
{
    TEST_ASSERT_EQUAL_UINT64(sizeof(last_request), request_size);
    last_request = *(ConstInterruptProtocolRequestPtr)request;
    request_sent = true;
    return Success(ChannelCode, CHANNEL_OK);
}

Result(OptionalChannelReplySize) fake_channel_poll_reply(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity)
{
    reply_polls++;
    if (pending_response_present)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT64(sizeof(pending_response), reply_capacity);
        *(InterruptProtocolResponsePtr)reply = pending_response;
        pending_response_present = false;
        return Success(OptionalChannelReplySize, Some(ChannelReplySize, sizeof(pending_response)));
    }
    return Success(OptionalChannelReplySize, None(ChannelReplySize));
}

Result(ChannelReplySize) fake_channel_submit(ChannelPtr self, ConstChannelDataPtr request, size_t request_size,
                                             ChannelDataPtr reply, size_t reply_capacity)
{
    TEST_ASSERT_EQUAL_UINT64(sizeof(last_request), request_size);
    last_request = *(ConstInterruptProtocolRequestPtr)request;
    request_sent = true;
    InterruptProtocolResponse response =
        InterruptProtocol__acknowledged(last_request.irq, last_request.generation, last_request.sequence);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(sizeof(response), reply_capacity);
    *(InterruptProtocolResponsePtr)reply = response;
    return Success(ChannelReplySize, sizeof(response));
}

void fake_channel_prepare_wait(void)
{
}

void fake_channel_wait_event(void)
{
}

ChannelCode fake_channel_close(ChannelPtr self)
{
    return CHANNEL_OK;
}

void test_channelio_converts_delivery_to_irq_event_and_acknowledges_same_sequence(void)
{
    pending_response = InterruptProtocol__delivery(48, 7, 19);
    pending_response_present = true;

    ServiceEvent event;
    TEST_ASSERT_EQUAL(SERVICE_OK, VirtioBlkChannelIO__wait(&event));
    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
    TEST_ASSERT_EQUAL_UINT64(48, event.source);
    TEST_ASSERT_EQUAL_UINT64(19, event.data);

    TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, VirtioBlkChannelIO__poll(&event));
    TEST_ASSERT_EQUAL_UINT(1, reply_polls);

    TEST_ASSERT_EQUAL(SERVICE_OK, VirtioBlkChannelIO__acknowledge(48));
    TEST_ASSERT_TRUE(request_sent);
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_WAIT, last_request.kind);
    TEST_ASSERT_EQUAL_UINT64(48, last_request.irq);
    TEST_ASSERT_EQUAL_UINT64(7, last_request.generation);
}

void test_channelio_poll_is_nonblocking_when_no_event_is_available(void)
{
    ServiceEvent event;

    TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, VirtioBlkChannelIO__poll(&event));
}

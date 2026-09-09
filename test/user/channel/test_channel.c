#include <stdint.h>

#include "unity.h"

#include "../../../common/ipc/block_channel.h"
#include "../../../common/ipc/echo_channel.h"
#include "../../../user/lib/channel/channel.c"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_channel_state_starts_with_zero_epochs(void)
{
    ChannelState state = ChannelState__new();

    TEST_ASSERT_EQUAL_UINT64(0, ChannelState__data_epoch(&state));
    TEST_ASSERT_EQUAL_UINT64(0, ChannelState__space_epoch(&state));
}

void test_channel_notify_data_advances_data_epoch_only(void)
{
    ChannelState state = ChannelState__new();
    Channel channel = Channel__new(&state);
    uint64_t observed_data = Channel__data_epoch(&channel);
    uint64_t observed_space = Channel__space_epoch(&channel);

    Channel__notify_data(&channel);

    TEST_ASSERT_TRUE(Channel__data_changed(&channel, observed_data));
    TEST_ASSERT_FALSE(Channel__space_changed(&channel, observed_space));
    TEST_ASSERT_EQUAL_UINT64(observed_data + 1, Channel__data_epoch(&channel));
    TEST_ASSERT_EQUAL_UINT64(observed_space, Channel__space_epoch(&channel));
}

void test_channel_notify_space_advances_space_epoch_only(void)
{
    ChannelState state = ChannelState__new();
    Channel channel = Channel__new(&state);
    uint64_t observed_data = Channel__data_epoch(&channel);
    uint64_t observed_space = Channel__space_epoch(&channel);

    Channel__notify_space(&channel);

    TEST_ASSERT_FALSE(Channel__data_changed(&channel, observed_data));
    TEST_ASSERT_TRUE(Channel__space_changed(&channel, observed_space));
    TEST_ASSERT_EQUAL_UINT64(observed_data, Channel__data_epoch(&channel));
    TEST_ASSERT_EQUAL_UINT64(observed_space + 1, Channel__space_epoch(&channel));
}

void test_channel_uses_state_pointer_as_identity(void)
{
    ChannelState state = ChannelState__new();
    Channel channel = Channel__new(&state);

    TEST_ASSERT_EQUAL_PTR(&state, Channel__state(&channel));
}

void test_block_channel_request_ring_push_pop_updates_epochs(void)
{
    BlockChannelMemory memory;
    BlockChannelMemory__init(&memory);
    BlockChannelRequest request = BlockChannelRequest__new(7, BLOCK_CHANNEL_READ, 3, 1, 0, 512);
    Channel channel = Channel__new(&memory.state.channel);
    uint64_t data_epoch = Channel__data_epoch(&channel);
    uint64_t space_epoch = Channel__space_epoch(&channel);

    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_OK, BlockChannelRequestRing__push(&memory.requests, &memory.state, &request));
    TEST_ASSERT_EQUAL_UINT64(data_epoch + 1, Channel__data_epoch(&channel));
    TEST_ASSERT_EQUAL_UINT64(space_epoch, Channel__space_epoch(&channel));

    BlockChannelRequest popped = BlockChannelRequest__new(0, BLOCK_CHANNEL_READ, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL(BLOCK_CHANNEL_OK, BlockChannelRequestRing__pop(&memory.requests, &memory.state, &popped));
    TEST_ASSERT_EQUAL_UINT64(request.request_id, popped.request_id);
    TEST_ASSERT_EQUAL_UINT64(request.block, popped.block);
    TEST_ASSERT_EQUAL_UINT64(space_epoch + 1, Channel__space_epoch(&channel));
}

void test_echo_channel_submit_take_and_respond_round_trips_bstring(void)
{
    EchoChannelMemory memory;
    EchoChannelMemory__init(&memory);
    EchoChannelMessage request = EchoChannelMessage__new();
    EchoChannelMessage response = EchoChannelMessage__new();
    BString message = bstr("echo me");
    BString echoed = empty_bstr();

    TEST_ASSERT_EQUAL(ECHO_CHANNEL_OK, EchoChannelMessage__copy_from_bstring(&request, 11, &message));
    TEST_ASSERT_EQUAL(ECHO_CHANNEL_OK, EchoChannelMemory__submit(&memory, &request));
    TEST_ASSERT_TRUE(EchoChannelMemory__request_ready(&memory));
    TEST_ASSERT_EQUAL(ECHO_CHANNEL_OK, EchoChannelMemory__take_request(&memory, &response));
    TEST_ASSERT_FALSE(EchoChannelMemory__request_ready(&memory));
    TEST_ASSERT_EQUAL_UINT64(11, response.request_id);

    TEST_ASSERT_EQUAL(ECHO_CHANNEL_OK, EchoChannelMemory__respond(&memory, &response));
    TEST_ASSERT_TRUE(EchoChannelMemory__response_ready(&memory));
    response = EchoChannelMessage__new();
    TEST_ASSERT_EQUAL(ECHO_CHANNEL_OK, EchoChannelMemory__take_response(&memory, &response));
    TEST_ASSERT_FALSE(EchoChannelMemory__response_ready(&memory));
    TEST_ASSERT_EQUAL(ECHO_CHANNEL_OK, EchoChannelMessage__copy_to_bstring(&response, &echoed));
    TEST_ASSERT_TRUE(BString__equals(&message, &echoed));
}

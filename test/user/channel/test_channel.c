#include <stdint.h>

#include "unity.h"

#include "../../../common/ipc/legacy_block_transport.h"
#include "../../../user/lib/channel/signal.c"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_channel_state_starts_with_zero_epochs(void)
{
    ChannelSignalState state = ChannelSignalState__new();

    TEST_ASSERT_EQUAL_UINT64(0, ChannelSignalState__data_epoch(&state));
    TEST_ASSERT_EQUAL_UINT64(0, ChannelSignalState__space_epoch(&state));
}

void test_channel_notify_data_advances_data_epoch_only(void)
{
    ChannelSignalState state = ChannelSignalState__new();
    ChannelSignal channel = ChannelSignal__new(&state);
    uint64_t observed_data = ChannelSignal__data_epoch(&channel);
    uint64_t observed_space = ChannelSignal__space_epoch(&channel);

    ChannelSignal__notify_data(&channel);

    TEST_ASSERT_TRUE(ChannelSignal__data_changed(&channel, observed_data));
    TEST_ASSERT_FALSE(ChannelSignal__space_changed(&channel, observed_space));
    TEST_ASSERT_EQUAL_UINT64(observed_data + 1, ChannelSignal__data_epoch(&channel));
    TEST_ASSERT_EQUAL_UINT64(observed_space, ChannelSignal__space_epoch(&channel));
}

void test_channel_notify_space_advances_space_epoch_only(void)
{
    ChannelSignalState state = ChannelSignalState__new();
    ChannelSignal channel = ChannelSignal__new(&state);
    uint64_t observed_data = ChannelSignal__data_epoch(&channel);
    uint64_t observed_space = ChannelSignal__space_epoch(&channel);

    ChannelSignal__notify_space(&channel);

    TEST_ASSERT_FALSE(ChannelSignal__data_changed(&channel, observed_data));
    TEST_ASSERT_TRUE(ChannelSignal__space_changed(&channel, observed_space));
    TEST_ASSERT_EQUAL_UINT64(observed_data, ChannelSignal__data_epoch(&channel));
    TEST_ASSERT_EQUAL_UINT64(observed_space + 1, ChannelSignal__space_epoch(&channel));
}

void test_channel_uses_state_pointer_as_identity(void)
{
    ChannelSignalState state = ChannelSignalState__new();
    ChannelSignal channel = ChannelSignal__new(&state);

    TEST_ASSERT_EQUAL_PTR(&state, ChannelSignal__state(&channel));
}

void test_block_protocol_request_ring_push_pop_updates_epochs(void)
{
    BlockProtocolMemory memory;
    BlockProtocolMemory__init(&memory);
    BlockProtocolRequest request = BlockProtocolRequest__new(7, BLOCK_PROTOCOL_READ, 3, 1, 0, 512);
    ChannelSignal channel = ChannelSignal__new(BlockProtocolMemory__channel_state(&memory));
    uint64_t data_epoch = ChannelSignal__data_epoch(&channel);
    uint64_t space_epoch = ChannelSignal__space_epoch(&channel);

    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__push_request(&memory, &request));
    TEST_ASSERT_EQUAL_UINT64(data_epoch + 1, ChannelSignal__data_epoch(&channel));
    TEST_ASSERT_EQUAL_UINT64(space_epoch, ChannelSignal__space_epoch(&channel));

    BlockProtocolRequest popped = BlockProtocolRequest__new(0, BLOCK_PROTOCOL_READ, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__pop_request(&memory, &popped));
    TEST_ASSERT_EQUAL_UINT64(request.request_id, popped.request_id);
    TEST_ASSERT_EQUAL_UINT64(request.block, popped.block);
    TEST_ASSERT_EQUAL_UINT64(space_epoch + 1, ChannelSignal__space_epoch(&channel));
}

void test_block_protocol_data_copy_is_bounded(void)
{
    BlockProtocolMemory memory;
    BlockProtocolMemory__init(&memory);
    uint8_t source[] = {1, 2, 3, 4};
    uint8_t target[] = {0, 0, 0, 0};

    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__write_data(&memory, 12, source, sizeof(source)));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, BlockProtocolMemory__read_data(&memory, 12, target, sizeof(target)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, target, sizeof(source));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT,
                      BlockProtocolMemory__write_data(&memory, BLOCK_PROTOCOL_DATA_SIZE - 1, source, sizeof(source)));
}

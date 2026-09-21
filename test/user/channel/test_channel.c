#include <stdint.h>

#include "unity.h"

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

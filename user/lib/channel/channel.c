#include "channel.h"

#define CHANNEL_ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define CHANNEL_ATOMIC_RELEASE __ATOMIC_RELEASE

ChannelState ChannelState__new(void)
{
    return (ChannelState){
        .data_epoch = 0,
        .space_epoch = 0,
        .consumer_closed = 0,
        .producer_closed = 0,
        .consumer_close_ack = 0,
        .producer_close_ack = 0,
    };
}

uint64_t ChannelState__data_epoch(ChannelStatePtr self)
{
    return __atomic_load_n(&self->data_epoch, CHANNEL_ATOMIC_ACQUIRE);
}

uint64_t ChannelState__space_epoch(ChannelStatePtr self)
{
    return __atomic_load_n(&self->space_epoch, CHANNEL_ATOMIC_ACQUIRE);
}

void ChannelState__notify_data(ChannelStatePtr self)
{
    __atomic_add_fetch(&self->data_epoch, 1, CHANNEL_ATOMIC_RELEASE);
}

void ChannelState__notify_space(ChannelStatePtr self)
{
    __atomic_add_fetch(&self->space_epoch, 1, CHANNEL_ATOMIC_RELEASE);
}

bool ChannelState__data_changed(ChannelStatePtr self, uint64_t observed_epoch)
{
    return ChannelState__data_epoch(self) != observed_epoch;
}

bool ChannelState__space_changed(ChannelStatePtr self, uint64_t observed_epoch)
{
    return ChannelState__space_epoch(self) != observed_epoch;
}

Channel Channel__new(ChannelStatePtr state)
{
    return (Channel){
        .state = state,
    };
}

ChannelStatePtr Channel__state(ChannelPtr self)
{
    return self->state;
}

uint64_t Channel__data_epoch(ChannelPtr self)
{
    return ChannelState__data_epoch(Channel__state(self));
}

uint64_t Channel__space_epoch(ChannelPtr self)
{
    return ChannelState__space_epoch(Channel__state(self));
}

void Channel__notify_data(ChannelPtr self)
{
    ChannelState__notify_data(Channel__state(self));
    Channel__signal(self);
}

void Channel__notify_space(ChannelPtr self)
{
    ChannelState__notify_space(Channel__state(self));
    Channel__signal(self);
}

bool Channel__data_changed(ChannelPtr self, uint64_t observed_epoch)
{
    return ChannelState__data_changed(Channel__state(self), observed_epoch);
}

bool Channel__space_changed(ChannelPtr self, uint64_t observed_epoch)
{
    return ChannelState__space_changed(Channel__state(self), observed_epoch);
}

void __attribute__((weak)) Channel__signal(ChannelPtr self)
{
}

void __attribute__((weak)) Channel__wait_for_data_change(ChannelPtr self, uint64_t observed_epoch)
{
    while (!Channel__data_changed(self, observed_epoch))
    {
    }
}

void __attribute__((weak)) Channel__wait_for_space_change(ChannelPtr self, uint64_t observed_epoch)
{
    while (!Channel__space_changed(self, observed_epoch))
    {
    }
}

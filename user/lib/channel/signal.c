#include "signal.h"

#define CHANNEL_ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define CHANNEL_ATOMIC_RELEASE __ATOMIC_RELEASE

ChannelSignalState ChannelSignalState__new(void)
{
    return (ChannelSignalState){
        .data_epoch = 0,
        .space_epoch = 0,
        .consumer_closed = 0,
        .producer_closed = 0,
        .consumer_close_ack = 0,
        .producer_close_ack = 0,
    };
}

uint64_t ChannelSignalState__data_epoch(ChannelSignalStatePtr self)
{
    return __atomic_load_n(&self->data_epoch, CHANNEL_ATOMIC_ACQUIRE);
}

uint64_t ChannelSignalState__space_epoch(ChannelSignalStatePtr self)
{
    return __atomic_load_n(&self->space_epoch, CHANNEL_ATOMIC_ACQUIRE);
}

void ChannelSignalState__notify_data(ChannelSignalStatePtr self)
{
    __atomic_add_fetch(&self->data_epoch, 1, CHANNEL_ATOMIC_RELEASE);
}

void ChannelSignalState__notify_space(ChannelSignalStatePtr self)
{
    __atomic_add_fetch(&self->space_epoch, 1, CHANNEL_ATOMIC_RELEASE);
}

bool ChannelSignalState__data_changed(ChannelSignalStatePtr self, uint64_t observed_epoch)
{
    return ChannelSignalState__data_epoch(self) != observed_epoch;
}

bool ChannelSignalState__space_changed(ChannelSignalStatePtr self, uint64_t observed_epoch)
{
    return ChannelSignalState__space_epoch(self) != observed_epoch;
}

ChannelSignal ChannelSignal__new(ChannelSignalStatePtr state)
{
    return (ChannelSignal){
        .state = state,
    };
}

ChannelSignalStatePtr ChannelSignal__state(ChannelSignalPtr self)
{
    return self->state;
}

uint64_t ChannelSignal__data_epoch(ChannelSignalPtr self)
{
    return ChannelSignalState__data_epoch(ChannelSignal__state(self));
}

uint64_t ChannelSignal__space_epoch(ChannelSignalPtr self)
{
    return ChannelSignalState__space_epoch(ChannelSignal__state(self));
}

void ChannelSignal__notify_data(ChannelSignalPtr self)
{
    ChannelSignalState__notify_data(ChannelSignal__state(self));
    ChannelSignal__signal(self);
}

void ChannelSignal__notify_space(ChannelSignalPtr self)
{
    ChannelSignalState__notify_space(ChannelSignal__state(self));
    ChannelSignal__signal(self);
}

bool ChannelSignal__data_changed(ChannelSignalPtr self, uint64_t observed_epoch)
{
    return ChannelSignalState__data_changed(ChannelSignal__state(self), observed_epoch);
}

bool ChannelSignal__space_changed(ChannelSignalPtr self, uint64_t observed_epoch)
{
    return ChannelSignalState__space_changed(ChannelSignal__state(self), observed_epoch);
}

ConstIpcEventPtr ChannelSignal__data_event(ChannelSignalPtr self)
{
    return (ConstIpcEventPtr)&ChannelSignal__state(self)->data_epoch;
}

ConstIpcEventPtr ChannelSignal__space_event(ChannelSignalPtr self)
{
    return (ConstIpcEventPtr)&ChannelSignal__state(self)->space_epoch;
}

void __attribute__((weak)) ChannelSignal__signal(ChannelSignalPtr self)
{
}

void __attribute__((weak)) ChannelSignal__wait_for_data_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    while (!ChannelSignal__data_changed(self, observed_epoch))
    {
    }
}

void __attribute__((weak)) ChannelSignal__wait_for_space_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    while (!ChannelSignal__space_changed(self, observed_epoch))
    {
    }
}

#ifndef __USER_LIB_CHANNEL_CHANNEL_H
#define __USER_LIB_CHANNEL_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "ipc/channel.h"

typedef struct Channel Channel;
typedef Channel * ChannelPtr;
typedef const Channel * ConstChannelPtr;

struct Channel
{
    ChannelStatePtr state;
};

ChannelState ChannelState__new(void);
uint64_t ChannelState__data_epoch(ChannelStatePtr self);
uint64_t ChannelState__space_epoch(ChannelStatePtr self);
void ChannelState__notify_data(ChannelStatePtr self);
void ChannelState__notify_space(ChannelStatePtr self);
bool ChannelState__data_changed(ChannelStatePtr self, uint64_t observed_epoch);
bool ChannelState__space_changed(ChannelStatePtr self, uint64_t observed_epoch);

Channel Channel__new(ChannelStatePtr state);
ChannelStatePtr Channel__state(ChannelPtr self);
uint64_t Channel__data_epoch(ChannelPtr self);
uint64_t Channel__space_epoch(ChannelPtr self);
void Channel__notify_data(ChannelPtr self);
void Channel__notify_space(ChannelPtr self);
bool Channel__data_changed(ChannelPtr self, uint64_t observed_epoch);
bool Channel__space_changed(ChannelPtr self, uint64_t observed_epoch);

void Channel__signal(ChannelPtr self);
void Channel__wait_for_data_change(ChannelPtr self, uint64_t observed_epoch);
void Channel__wait_for_space_change(ChannelPtr self, uint64_t observed_epoch);

#endif

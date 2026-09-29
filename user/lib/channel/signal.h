#ifndef HUND_USER_CHANNEL_SIGNAL_H
#define HUND_USER_CHANNEL_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "ipc/channel_signal.h"
#include "ipc/doorbell.h"

typedef struct ChannelSignal ChannelSignal;
typedef ChannelSignal * ChannelSignalPtr;
typedef const ChannelSignal * ConstChannelSignalPtr;

struct ChannelSignal
{
    ChannelSignalStatePtr state;
};

ChannelSignalState ChannelSignalState__new(void);
uint64_t ChannelSignalState__data_epoch(ChannelSignalStatePtr self);
uint64_t ChannelSignalState__space_epoch(ChannelSignalStatePtr self);
void ChannelSignalState__notify_data(ChannelSignalStatePtr self);
void ChannelSignalState__notify_space(ChannelSignalStatePtr self);
bool ChannelSignalState__data_changed(ChannelSignalStatePtr self, uint64_t observed_epoch);
bool ChannelSignalState__space_changed(ChannelSignalStatePtr self, uint64_t observed_epoch);

ChannelSignal ChannelSignal__new(ChannelSignalStatePtr state);
ChannelSignalStatePtr ChannelSignal__state(ChannelSignalPtr self);
uint64_t ChannelSignal__data_epoch(ChannelSignalPtr self);
uint64_t ChannelSignal__space_epoch(ChannelSignalPtr self);
void ChannelSignal__notify_data(ChannelSignalPtr self);
void ChannelSignal__notify_space(ChannelSignalPtr self);
bool ChannelSignal__data_changed(ChannelSignalPtr self, uint64_t observed_epoch);
bool ChannelSignal__space_changed(ChannelSignalPtr self, uint64_t observed_epoch);
ConstIpcEventPtr ChannelSignal__data_event(ChannelSignalPtr self);
ConstIpcEventPtr ChannelSignal__space_event(ChannelSignalPtr self);

void ChannelSignal__signal(ChannelSignalPtr self);
void ChannelSignal__wait_for_data_change(ChannelSignalPtr self, uint64_t observed_epoch);
void ChannelSignal__wait_for_space_change(ChannelSignalPtr self, uint64_t observed_epoch);

#endif

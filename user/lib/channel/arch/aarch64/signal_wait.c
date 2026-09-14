#include "../../signal.h"

void ChannelSignal__signal(ChannelSignalPtr self)
{
    __asm__ __volatile__("sev" ::: "memory");
}

void ChannelSignal__wait_for_data_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    __asm__ __volatile__("sevl" ::: "memory");
    while (!ChannelSignal__data_changed(self, observed_epoch))
    {
        __asm__ __volatile__("wfe" ::: "memory");
    }
}

void ChannelSignal__wait_for_space_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    __asm__ __volatile__("sevl" ::: "memory");
    while (!ChannelSignal__space_changed(self, observed_epoch))
    {
        __asm__ __volatile__("wfe" ::: "memory");
    }
}

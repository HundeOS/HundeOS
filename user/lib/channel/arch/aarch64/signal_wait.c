#include "../../signal.h"
#include "ipc/ipc_platform.h"

void ChannelSignal__signal(ChannelSignalPtr self)
{
    IpcPlatform__signal_event();
}

void ChannelSignal__wait_for_data_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    __asm__ __volatile__("sevl" ::: "memory");
    while (!ChannelSignal__data_changed(self, observed_epoch))
    {
        IpcPlatform__wait_event();
    }
}

void ChannelSignal__wait_for_space_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    __asm__ __volatile__("sevl" ::: "memory");
    while (!ChannelSignal__space_changed(self, observed_epoch))
    {
        IpcPlatform__wait_event();
    }
}

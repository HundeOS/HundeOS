#include "../../signal.h"
#include "ipc/ipc_platform.h"

void ChannelSignal__signal(ChannelSignalPtr self)
{
    IpcPlatform__signal_event();
}

void ChannelSignal__wait_for_data_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    while (!ChannelSignal__data_changed(self, observed_epoch))
    {
        ConstIpcEventPtr event = ChannelSignal__data_event(self);
        IpcEventGeneration observed = IpcPlatform__prepare_wait(event);
        if (!ChannelSignal__data_changed(self, observed_epoch))
        {
            IpcPlatform__wait_event(event, observed);
        }
    }
}

void ChannelSignal__wait_for_space_change(ChannelSignalPtr self, uint64_t observed_epoch)
{
    while (!ChannelSignal__space_changed(self, observed_epoch))
    {
        ConstIpcEventPtr event = ChannelSignal__space_event(self);
        IpcEventGeneration observed = IpcPlatform__prepare_wait(event);
        if (!ChannelSignal__space_changed(self, observed_epoch))
        {
            IpcPlatform__wait_event(event, observed);
        }
    }
}

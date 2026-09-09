#include "../../channel.h"
#include "../../exchange.h"

void ChannelExchange__prepare_wait(void)
{
    __asm__ volatile("sevl" ::: "memory");
}

void ChannelExchange__wait_event(void)
{
    __asm__ volatile("wfe" ::: "memory");
}

void Channel__signal(ChannelPtr self)
{
    __asm__ __volatile__("sev" ::: "memory");
}

void Channel__wait_for_data_change(ChannelPtr self, uint64_t observed_epoch)
{
    __asm__ __volatile__("sevl" ::: "memory");
    while (!Channel__data_changed(self, observed_epoch))
    {
        __asm__ __volatile__("wfe" ::: "memory");
    }
}

void Channel__wait_for_space_change(ChannelPtr self, uint64_t observed_epoch)
{
    __asm__ __volatile__("sevl" ::: "memory");
    while (!Channel__space_changed(self, observed_epoch))
    {
        __asm__ __volatile__("wfe" ::: "memory");
    }
}

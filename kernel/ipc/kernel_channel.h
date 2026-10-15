#ifndef KERNEL_IPC_KERNEL_CHANNEL_H
#define KERNEL_IPC_KERNEL_CHANNEL_H

#include "../../common/ipc/channel.h"
#include "../scheduler/scheduler.h"

typedef enum KernelChannelPhase
{
    KERNEL_CHANNEL_CONNECTING,
    KERNEL_CHANNEL_OPEN,
    KERNEL_CHANNEL_CLOSED,
} KernelChannelPhase;

typedef struct KernelChannel
{
    Channel channel;
    KernelChannelPhase phase;
    ProcessID server_pid;
    ProcessID origin_pid;
} KernelChannel;

typedef KernelChannel * KernelChannelPtr;
ResultDef(KernelChannel);

// All operations require the scheduler lock, including close. No operation waits.
Result(KernelChannel)
    KernelChannel__open(SchedulerPtr scheduler, ServiceDirectoryPtr directory, ConstBStringPtr name, ProcessPtr origin);
QueueProgress KernelChannel__progress(KernelChannelPtr self);
Result(ChannelCode)
    KernelChannel__request(KernelChannelPtr self, SchedulerPtr scheduler, ConstChannelDataPtr request, size_t size);
Result(OptionalChannelReplySize)
    KernelChannel__poll(KernelChannelPtr self, SchedulerPtr scheduler, ChannelDataPtr reply, size_t capacity);
void KernelChannel__close(KernelChannelPtr self, SchedulerPtr scheduler);
#endif

#include "kernel_channel.h"

static BString unavailable = bstr("Kernel channel unavailable");

static KernelChannel KernelChannel__new(Queue control, ProcessID server, ProcessID origin)
{
    KernelChannel self = {
        .phase = KERNEL_CHANNEL_CONNECTING,
        .server_pid = server,
        .origin_pid = origin,
    };
    self.channel = Channel__new_with_origin(&control, origin);
    return self;
}

Result(KernelChannel)
    KernelChannel__open(SchedulerPtr scheduler, ServiceDirectoryPtr directory, ConstBStringPtr name, ProcessPtr origin)
{
    QueueEndpointID target = unwrap_optional(ServiceDirectory__find_endpoint(directory, name), 0);
    ProcessID server = unwrap_optional(ServiceDirectory__find_service_pid(directory, name), 0);
    if (target == 0 || server == 0)
    {
        return Error(KernelChannel, unavailable, CHANNEL_UNAVAILABLE);
    }
    match_result(
        Queues__connect_kernel(&scheduler->queues, target, &Some(ProcessPtr, origin)),
        success(control)
        {
            return Success(KernelChannel, KernelChannel__new(control, server, origin->pid));
        },
        error(code, message)
        {
            return Error(KernelChannel, message, code);
        }
    );
}

static Result(ChannelCode) KernelChannel__advance(KernelChannelPtr self, SchedulerPtr scheduler)
{
    if (self->phase == KERNEL_CHANNEL_CLOSED)
    {
        return Error(ChannelCode, unavailable, CHANNEL_UNAVAILABLE);
    }
    if (self->phase == KERNEL_CHANNEL_CONNECTING)
    {
        QueueCode status = Queue__status(&self->channel.control);
        if (status == QUEUE_PENDING)
        {
            return Success(ChannelCode, CHANNEL_PENDING);
        }
        if (status != QUEUE_OK)
        {
            return Error(ChannelCode, unavailable, CHANNEL_UNAVAILABLE);
        }
        self->phase = KERNEL_CHANNEL_OPEN;
    }
    if (Queue__status(&self->channel.control) != QUEUE_OK)
    {
        return Error(ChannelCode, unavailable, CHANNEL_UNAVAILABLE);
    }
    return Success(ChannelCode, CHANNEL_OK);
}

QueueProgress KernelChannel__progress(KernelChannelPtr self)
{
    return Queue__progress(&self->channel.control);
}

Result(ChannelCode)
    KernelChannel__request(KernelChannelPtr self, SchedulerPtr scheduler, ConstChannelDataPtr request, size_t size)
{
    match_result(
        KernelChannel__advance(self, scheduler),
        success(code)
        {
            if (code == CHANNEL_PENDING)
            {
                return Success(ChannelCode, code);
            }
            return Channel__try_request(&self->channel, request, size);
        },
        error(code, message)
        {
            return Error(ChannelCode, message, code);
        }
    );
}

Result(OptionalChannelReplySize)
    KernelChannel__poll(KernelChannelPtr self, SchedulerPtr scheduler, ChannelDataPtr reply, size_t capacity)
{
    match_result(
        KernelChannel__advance(self, scheduler),
        success(code)
        {
            if (code == CHANNEL_PENDING)
            {
                return Success(OptionalChannelReplySize, None(ChannelReplySize));
            }
            return Channel__try_poll_reply(&self->channel, reply, capacity);
        },
        error(code, message)
        {
            return Error(OptionalChannelReplySize, message, code);
        }
    );
}

void KernelChannel__close(KernelChannelPtr self, SchedulerPtr scheduler)
{
    if (self->phase != KERNEL_CHANNEL_CLOSED)
    {
        Queues__close_kernel(&scheduler->queues, Queue__connection(&self->channel.control));
        self->phase = KERNEL_CHANNEL_CLOSED;
    }
}

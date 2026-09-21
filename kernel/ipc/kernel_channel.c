#include "kernel_channel.h"

static BString unavailable = bstr("Kernel channel unavailable");

static KernelChannel KernelChannel__new(Queue control, QueueEndpointID target, ProcessID server, ProcessID origin)
{
    KernelChannel self = {
        .phase = KERNEL_CHANNEL_CONNECTING,
        .target = target,
        .server_pid = server,
        .origin_pid = origin,
    };
    self.channel.control = control;
    return self;
}

Result(KernelChannel)
    KernelChannel__open(SchedulerPtr scheduler, ServiceDirectoryPtr directory, ConstBStringPtr name, ProcessPtr origin)
{
    QueueEndpointID target = unwrap_optional(ServiceDirectory__find_endpoint(directory, name), 0);
    QueueEndpointID warden =
        unwrap_optional(ServiceDirectory__find_endpoint(directory, &bstr(CHANNEL_WARDEN_SERVICE_NAME)), 0);
    ProcessID server = unwrap_optional(ServiceDirectory__find_service_pid(directory, name), 0);
    if (target == 0 || warden == 0 || server == 0)
    {
        return Error(KernelChannel, unavailable, CHANNEL_UNAVAILABLE);
    }
    match_result(
        Queues__connect_kernel(&scheduler->queues, warden, &Some(ProcessPtr, origin)),
        success(control)
        {
            return Success(KernelChannel, KernelChannel__new(control, target, server, origin->pid));
        },
        error(code, message)
        {
            return Error(KernelChannel, message, code);
        }
    );
}

static Result(ChannelCode) KernelChannel__payload(KernelChannelPtr self, SchedulerPtr scheduler)
{
    if (self->payload_address < MEM_SHARED_PAGE_BASE ||
        self->payload_address >= MEM_SHARED_PAGE_BASE + MEM_SHARED_PAGE_COUNT * MEM_SHARED_PAGE_SIZE ||
        self->payload_address % MEM_SHARED_PAGE_SIZE != 0)
    {
        return Error(ChannelCode, unavailable, CHANNEL_INVALID_REPLY);
    }
    ArrayIndex index = (self->payload_address - MEM_SHARED_PAGE_BASE) / MEM_SHARED_PAGE_SIZE;
    match_optional(
        ManagedSharedPages__at(&scheduler->shared_pages, index),
        some(page)
        {
            if (page.address.valid && page.owner_pid == Queue__peer_pid(&self->channel.control) &&
                page.first_peer_pid == QUEUE_KERNEL_PID && page.second_peer_pid == self->server_pid &&
                (self->phase != KERNEL_CHANNEL_OPEN || page.generation == self->payload_generation))
            {
                self->payload_generation = page.generation;
                self->channel.payload = k_v_ptr(page.address, ChannelPayload);
                return Success(ChannelCode, CHANNEL_OK);
            }
        },
        none_do_nothing()
    );
    return Error(ChannelCode, unavailable, CHANNEL_UNAVAILABLE);
}

static Result(ChannelCode) KernelChannel__advance(KernelChannelPtr self, SchedulerPtr scheduler)
{
    if (self->phase == KERNEL_CHANNEL_CLOSED)
    {
        return Error(ChannelCode, unavailable, CHANNEL_UNAVAILABLE);
    }
    if (self->phase == KERNEL_CHANNEL_CONNECTING)
    {
        QueueMessage request = ChannelControlMessage__new(CHANNEL_REQUEST_OPEN, self->target, 0,
                                                          Queue__connection(&self->channel.control));
        match_result(
            Queue__push(&self->channel.control, &request),
            success(code)
            {
                self->phase = KERNEL_CHANNEL_OPENING;
            },
            error(code, message)
            {
                if (code == QUEUE_PENDING || code == QUEUE_FULL)
                {
                    return Success(ChannelCode, CHANNEL_PENDING);
                }
                return Error(ChannelCode, message, code);
            }
        );
    }
    if (self->phase == KERNEL_CHANNEL_OPENING)
    {
        match_result(
            Queue__pop(&self->channel.control),
            success(reply)
            {
                if (reply.kind != CHANNEL_RESPONSE_OPEN || reply.arg2 != Queue__connection(&self->channel.control))
                {
                    return Error(ChannelCode, unavailable, CHANNEL_INVALID_REPLY);
                }
                self->payload_address = reply.arg0;
                match_result(
                    KernelChannel__payload(self, scheduler),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return Error(ChannelCode, message, code);
                    }
                );
                self->channel =
                    Channel__new_with_origin(&self->channel.control, self->channel.payload, self->origin_pid);
                self->phase = KERNEL_CHANNEL_OPEN;
            },
            error(code, message)
            {
                if (code == QUEUE_PENDING || code == QUEUE_EMPTY)
                {
                    return Success(ChannelCode, CHANNEL_PENDING);
                }
                return Error(ChannelCode, message, code);
            }
        );
    }
    if (Queue__status(&self->channel.control) != QUEUE_OK)
    {
        return Error(ChannelCode, unavailable, CHANNEL_UNAVAILABLE);
    }
    return KernelChannel__payload(self, scheduler);
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

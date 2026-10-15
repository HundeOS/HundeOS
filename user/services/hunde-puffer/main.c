#include "puffer.h"
#include "channel/server.h"
#include "sys/service.h"
#include "trace/io_trace.h"

typedef struct PufferConnection
{
    ChannelServer channel;
    BufferReply reply;
    bool pending;
} PufferConnection;

typedef PufferConnection * PufferConnectionPtr;
OptionalDef(PufferConnection);

static PufferConnection PufferConnection__new(ChannelServer channel)
{
    return (PufferConnection){.channel = channel};
}

static bool PufferConnection__equals(void * first, void * second)
{
    return ChannelServer__connection(&((PufferConnectionPtr)first)->channel) ==
           ChannelServer__connection(&((PufferConnectionPtr)second)->channel);
}

FixedArrayDefine(PufferConnection, QUEUE_LIMIT, PufferConnections, PufferConnection__equals);
FixedArrayBodyDefine(PufferConnections);

static bool PufferConnection__matches(PufferConnectionPtr self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionIDPtr)context;
}

static bool PufferConnection__serve(PufferConnectionPtr self, PufferPtr manager)
{
    if (!self->pending)
    {
        match_result(
            ChannelServer__poll(&self->channel),
            success(incoming)
            {
                match_optional(
                    incoming,
                    some(metadata)
                    {
                        BufferRequest request;
                        self->reply = BufferReply__new(BUFFER_INVALID);
                        if (metadata.size == sizeof(request) &&
                            ChannelServer__read_request(&self->channel, (ChannelDataPtr)&request, sizeof(request)))
                        {
                            self->reply =
                                Puffer__execute(manager, ChannelServer__transport_pid(&self->channel), &request);
                        }
                        self->pending = true;
                    },
                    none()
                    {
                        return false;
                    }
                );
            },
            error(code, message)
            {
                ChannelServer__close(&self->channel);
                return true;
            }
        );
    }
    match_result(
        ChannelServer__reply(&self->channel, (ConstChannelDataPtr)&self->reply, sizeof(self->reply)),
        success(code)
        {
            self->pending = false;
            return true;
        },
        error(code, message)
        {
            if (code != CHANNEL_PENDING)
            {
                ChannelServer__close(&self->channel);
            }
            return false;
        }
    );
}

int main(void)
{
    IOTrace__set_enabled(false);
    static Puffer manager;
    static PufferConnections connections;
    manager = Puffer__new(ServiceDirectory__shared());
    connections = PufferConnections__new();
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    int64_t mapped = BufferMemory__operation(MEM_BUFFER_REGISTER, 0, 0, 0, 0);
    if (mapped < 0)
    {
        return 1;
    }
    KernelNotificationQueuePtr events = (KernelNotificationQueuePtr)(Address)mapped;
    QueueArrivalsPtr arrivals;
    match_result(
        Queue__listen(&bstr(BUFFER_SERVICE_NAME)),
        success(value)
        {
            arrivals = value;
        },
        error(code, message)
        {
            return 1;
        }
    );
    if (!service_ok(service_publish_ready(&bstr(BUFFER_SERVICE_NAME))))
    {
        return 1;
    }
    Queue__prepare_wait();
    while (true)
    {
        bool progress = false;
        QueueParticipantID warden = unwrap_optional(
            ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(CHANNEL_WARDEN_SERVICE_NAME)), 0);
        for_each_atomic_fa_ptr(
            QueueConnectionSlots,
            &arrivals->connections,
            index,
            id,
            {
                if (id == 0)
                {
                    continue;
                }
                match_find_result(
                    PufferConnections__find(&connections, PufferConnection__matches, &id),
                    some(found)
                    {
                        continue;
                    },
                    none_do_nothing()
                );
                match_result(
                    ChannelServer__accept(id),
                    success(channel)
                    {
                        match_result(
                            PufferConnections__push(&connections, PufferConnection__new(channel)),
                            success_do_nothing(),
                            error(code, message)
                            {
                                ChannelServer__close(&channel);
                            }
                        );
                    },
                    error(code, message)
                    {
                        QueueConnection__close(id);
                    }
                );
                progress = true;
            }
        );
        for (ArrayIndex index = 0; index < PufferConnections__len(&connections);)
        {
            match_optional(
                PufferConnections__ptr_at(&connections, index),
                some(connection)
                {
                    if (ChannelServer__status(&connection->channel) != QUEUE_OK)
                    {
                        PufferConnection removed = *connection;
                        ChannelServer__close(&removed.channel);
                        PufferConnections__remove(&connections, removed);
                        progress = true;
                        continue;
                    }
                    progress = PufferConnection__serve(connection, &manager) || progress;
                },
                none_do_nothing()
            );
            index++;
        }
        for (unsigned count = 0; count < BUFFER_LIMIT; count++)
        {
            match_optional(
                KernelNotificationQueue__pop(events),
                some(event)
                {
                    Puffer__reconcile(&manager, event.sequence);
                    progress = true;
                },
                none()
                {
                    break;
                }
            );
        }
        Puffer__housekeeping(&manager);
        if (!progress)
        {
            Queue__wait_event();
        }
    }
}

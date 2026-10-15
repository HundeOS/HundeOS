#include "channel/protocol.h"
#include "types/fixed_array.h"

typedef struct EchoConnection
{
    Queue queue;
    QueueMessage reply;
    bool pending;
    bool handled;
} EchoConnection;

typedef EchoConnection * EchoConnectionPtr;
OptionalDef(EchoConnection);

static EchoConnection EchoConnection__new(QueuePtr queue)
{
    return (EchoConnection){.queue = *queue};
}

static bool EchoConnection__equals(void * first, void * second)
{
    EchoConnectionPtr a = first;
    EchoConnectionPtr b = second;
    return Queue__connection(&a->queue) == Queue__connection(&b->queue);
}

FixedArrayDefine(EchoConnection, QUEUE_LIMIT, EchoConnections, &EchoConnection__equals);
FixedArrayBodyDefine(EchoConnections);
typedef EchoConnections * EchoConnectionsPtr;

static bool EchoConnection__matches_id(EchoConnectionPtr self, void * context)
{
    return Queue__connection(&self->queue) == *(QueueConnectionIDPtr)context;
}

static bool EchoConnections__contains(EchoConnectionsPtr self, QueueConnectionID id)
{
    match_find_result(
        EchoConnections__find(self, EchoConnection__matches_id, &id),
        some(connection)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static bool EchoConnections__step(EchoConnectionsPtr self, QueueArrivalsPtr arrivals, uint64_t warden_pid)
{
    bool work = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &arrivals->connections,
        index,
        id,
        {
            if (id == 0 || EchoConnections__contains(self, id))
            {
                continue;
            }
            match_result(
                Queue__accept(id),
                success(queue)
                {
                    if (!Queue__is_channel(&queue))
                    {
                        QueueConnection__close(id);
                    }
                    else
                    {
                        EchoConnection connection = EchoConnection__new(&queue);
                        match_result(
                            EchoConnections__push(self, connection),
                            success_do_nothing(),
                            error(code, message)
                            {
                                QueueConnection__close(id);
                            }
                        );
                    }
                },
                error(code, message)
                {
                    QueueConnection__close(id);
                }
            );
            work = true;
        }
    );
    for_each_fa_ptr(
        EchoConnections,
        self,
        index,
        value,
        {
            match_optional(
                EchoConnections__ptr_at(self, index),
                some(connection)
                {
                    bool close = Queue__status(&connection->queue) == QUEUE_CLOSED;
                    if (!close && connection->pending)
                    {
                        match_result(
                            Queue__push(&connection->queue, &connection->reply),
                            success(status)
                            {
                                connection->pending = false;
                                connection->handled = false;
                                work = true;
                            },
                            error(code, message)
                            {
                                if (code != QUEUE_FULL)
                                {
                                    close = true;
                                }
                            }
                        );
                    }
                    if (!close && !connection->handled)
                    {
                        match_result(
                            Queue__pop(&connection->queue),
                            success(request)
                            {
                                connection->handled = true;
                                connection->reply =
                                    ChannelControlMessage__new(CHANNEL_RESPONSE_FAILED, 0, 0, request.arg2);
                                connection->reply.request = request.request;
                                if (request.kind == CHANNEL_REQUEST_SUBMIT && request.arg0 == 0 &&
                                    request.arg1 < CHANNEL_PAYLOAD_CAPACITY)
                                {
                                    uint8_t request_data[CHANNEL_PAYLOAD_CAPACITY];
                                    if (!Queue__read_data(&connection->queue, request_data, request.arg1) ||
                                        !Queue__write_data(&connection->queue, request_data, request.arg1, 0))
                                    {
                                        connection->pending = true;
                                        work = true;
                                        continue;
                                    }
                                    // No payload access after publishing this completion, even under backpressure.
                                    connection->reply = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0,
                                                                                   request.arg1, request.arg2);
                                    connection->reply.request = request.request;
                                }
                                connection->pending = true;
                                work = true;
                            },
                            error(code, message)
                            {
                                if (code != QUEUE_EMPTY)
                                {
                                    close = true;
                                }
                            }
                        );
                    }
                    if (close && QueueConnection__close(Queue__connection(&connection->queue)) == QUEUE_OK)
                    {
                        EchoConnections__remove(self, value);
                        work = true;
                        break;
                    }
                },
                none_do_nothing()
            );
        }
    );
    return work;
}

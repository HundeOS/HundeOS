#include <string.h>

#include "channel/exchange.h"
#include "sys/io.h"
#include "sys/print.h"
#include "sys/service.h"
#include "types/fixed_array.h"

#ifndef HUNDEKANAL_TEST_ECHO_ENTRYPOINT
#define HUNDEKANAL_TEST_ECHO_ENTRYPOINT main
#endif

typedef struct EchoConnection
{
    AtomicMessageQueue queue;
    ServiceQueueMessage reply;
    bool pending;
    bool handled;
} EchoConnection;
typedef EchoConnection * EchoConnectionPtr;
OptionalDef(EchoConnection);

static EchoConnection EchoConnection__new(AtomicMessageQueuePtr queue)
{
    return (EchoConnection){.queue = *queue};
}

static bool EchoConnection__equals(void * first, void * second)
{
    EchoConnectionPtr a = first;
    EchoConnectionPtr b = second;
    return a->queue.connection == b->queue.connection;
}
FixedArrayDefine(EchoConnection, ATOMIC_MESSAGE_QUEUE_LIMIT, EchoConnections, &EchoConnection__equals);
FixedArrayBodyDefine(EchoConnections);
typedef EchoConnections * EchoConnectionsPtr;

static bool EchoConnection__matches_id(EchoConnectionPtr self, void * context)
{
    return self->queue.connection == *(AtomicMessageQueueConnectionID *)context;
}

static bool EchoConnections__contains(EchoConnectionsPtr self, AtomicMessageQueueConnectionID id)
{
    match_find_result(
        EchoConnections__find(self, EchoConnection__matches_id, &id),
        some(connection) { return true; },
        none_do_nothing()
    );
    return false;
}

static bool EchoConnections__step(EchoConnectionsPtr self, AtomicMessageQueueArrivalsPtr arrivals, uint64_t warden_pid)
{
    bool work = false;
    for_each_atomic_fa_ptr(AtomicMessageQueueConnectionSlots, &arrivals->connections, index, id, {
        if (id == 0 || EchoConnections__contains(self, id))
        {
            continue;
        }
        match_result(
            atomic_message_queue_accept(id),
            success(queue)
            {
                if (warden_pid == 0 || queue.peer_pid != warden_pid)
                {
                    atomic_message_queue_close(id);
                }
                else
                {
                    EchoConnection connection = EchoConnection__new(&queue);
                    match_result(
                        EchoConnections__push(self, connection),
                        success_do_nothing(),
                        error(code, message) { atomic_message_queue_close(id); }
                    );
                }
            },
            error(code, message) { atomic_message_queue_close(id); }
        );
        work = true;
    });
    for_each_fa_ptr(EchoConnections, self, index, value, {
        match_optional(
            EchoConnections__ptr_at(self, index),
            some(connection)
            {
                bool close = AtomicMessageQueue__status(&connection->queue) == ATOMIC_MESSAGE_QUEUE_CLOSED;
                if (!close && connection->pending)
                {
                    match_result(
                        AtomicMessageQueue__push(&connection->queue, &connection->reply),
                        success(status)
                        {
                            connection->pending = false;
                            work = true;
                        },
                        error(code, message)
                        {
                            if (code != ATOMIC_MESSAGE_QUEUE_FULL)
                            {
                                close = true;
                            }
                        }
                    );
                }
                if (!close && !connection->handled)
                {
                    match_result(
                        AtomicMessageQueue__pop(&connection->queue),
                        success(request)
                        {
                            connection->handled = true;
                            connection->reply = ChannelExchange__message(CHANNEL_RESPONSE_FAILED, 0, 0, request.arg2);
                            if (request.kind == CHANNEL_REQUEST_SERVE && request.arg0 != 0 &&
                                request.arg1 < CHANNEL_EXCHANGE_CAPACITY)
                            {
                                ChannelPayloadPtr payload = (ChannelPayloadPtr)request.arg0;
                                memcpy(payload->reply, payload->request, request.arg1);
                                // No payload access after publishing this completion, even under backpressure.
                                connection->reply =
                                    ChannelExchange__message(CHANNEL_RESPONSE_REPLIED, 0, request.arg1, request.arg2);
                            }
                            connection->pending = true;
                            work = true;
                        },
                        error(code, message)
                        {
                            if (code != ATOMIC_MESSAGE_QUEUE_EMPTY)
                            {
                                close = true;
                            }
                        }
                    );
                }
                if (close && atomic_message_queue_close(connection->queue.connection) == ATOMIC_MESSAGE_QUEUE_OK)
                {
                    EchoConnections__remove(self, value);
                    work = true;
                    break;
                }
            },
            none_do_nothing()
        );
    });
    return work;
}

int HUNDEKANAL_TEST_ECHO_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    match_result(
        atomic_message_queue_listen(&bstr("hundekanal-test-echo")),
        success(arrivals)
        {
            if (!service_ok(service_publish_ready(&bstr("hundekanal-test-echo"))))
            {
                return 1;
            }
            EchoConnections connections = EchoConnections__new();
            IO__print(&bstr("hundekanal-test-echo: ready\n"));
            ChannelExchange__prepare_wait();
            while (true)
            {
                uint64_t warden_pid = 0;
                match_optional(
                    ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr("hundekanal-warter")),
                    some(pid) { warden_pid = pid; },
                    none_do_nothing()
                );
                if (!EchoConnections__step(&connections, arrivals, warden_pid))
                {
                    ChannelExchange__wait_event();
                }
            }
        },
        error(code, message) { IO__printf(&bstr("hundekanal-test-echo: listen failed %i\n"), (int)code); }
    );
    return 1;
}

#include <string.h>

#include "channel/protocol.h"
#include "services/service_directory.h"
#include "sys/io.h"
#include "sys/print.h"
#include "sys/service.h"
#include "types/fixed_array.h"

#ifndef HUNDEKANAL_TEST_ECHO_ENTRYPOINT
#define HUNDEKANAL_TEST_ECHO_ENTRYPOINT main
#endif

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
    return Queue__connection(&self->queue) == *(QueueConnectionID *)context;
}

static bool EchoConnections__contains(EchoConnectionsPtr self, QueueConnectionID id)
{
    match_find_result(
        EchoConnections__find(self, EchoConnection__matches_id, &id),
        some(connection) { return true; },
        none_do_nothing()
    );
    return false;
}

static bool EchoConnections__step(EchoConnectionsPtr self, QueueArrivalsPtr arrivals, uint64_t warden_pid)
{
    bool work = false;
    for_each_atomic_fa_ptr(QueueConnectionSlots, &arrivals->connections, index, id, {
        if (id == 0 || EchoConnections__contains(self, id))
        {
            continue;
        }
        match_result(
            Queue__accept(id),
            success(queue)
            {
                if (warden_pid == 0 || Queue__peer_pid(&queue) != warden_pid)
                {
                    QueueConnection__close(id);
                }
                else
                {
                    EchoConnection connection = EchoConnection__new(&queue);
                    match_result(
                        EchoConnections__push(self, connection),
                        success_do_nothing(),
                        error(code, message) { QueueConnection__close(id); }
                    );
                }
            },
            error(code, message) { QueueConnection__close(id); }
        );
        work = true;
    });
    for_each_fa_ptr(EchoConnections, self, index, value, {
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
                            connection->reply = ChannelControlMessage__new(CHANNEL_RESPONSE_FAILED, 0, 0, request.arg2);
                            if (request.kind == CHANNEL_REQUEST_SERVE && request.arg0 != 0 &&
                                request.arg1 < CHANNEL_PAYLOAD_CAPACITY)
                            {
                                ChannelPayloadPtr payload = (ChannelPayloadPtr)request.arg0;
                                uint8_t request_data[CHANNEL_PAYLOAD_CAPACITY];
                                if (!ChannelPayload__read_request(payload, request_data, request.arg1) ||
                                    !ChannelPayload__write_reply(payload, request_data, request.arg1))
                                {
                                    connection->pending = true;
                                    work = true;
                                    continue;
                                }
                                // No payload access after publishing this completion, even under backpressure.
                                connection->reply =
                                    ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, request.arg1, request.arg2);
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
        Queue__listen(&bstr("hundekanal-test-echo")),
        success(arrivals)
        {
            if (!service_ok(service_publish_ready(&bstr("hundekanal-test-echo"))))
            {
                return 1;
            }
            EchoConnections connections = EchoConnections__new();
            IO__print(&bstr("hundekanal-test-echo: ready\n"));
            Queue__prepare_wait();
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
                    Queue__wait_event();
                }
            }
        },
        error(code, message) { IO__printf(&bstr("hundekanal-test-echo: listen failed %i\n"), (int)code); }
    );
    return 1;
}

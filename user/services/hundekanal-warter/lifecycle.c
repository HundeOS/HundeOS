#include "lifecycle.h"

#include "sys/mem.h"
#include "sys/process.h"

#define ChannelExchangeState__new(state_action) {.action = state_action}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__wait_open(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__connecting(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__delivering_open(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__open(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__submitting(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__working(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__working_client_closed(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__closing_server(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__delivering_reply(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__wait_client_close(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__releasing(ChannelExchangePtr self);
static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__complete(ChannelExchangePtr self);
static bool ChannelExchange__matches_connection(ChannelExchangePtr self, void * context);

const ChannelExchangeState CHANNEL_EXCHANGE_WAIT_OPEN = ChannelExchangeState__new(&ChannelExchangeState__wait_open);
const ChannelExchangeState CHANNEL_EXCHANGE_CONNECTING = ChannelExchangeState__new(&ChannelExchangeState__connecting);
const ChannelExchangeState CHANNEL_EXCHANGE_DELIVERING_OPEN =
    ChannelExchangeState__new(&ChannelExchangeState__delivering_open);
const ChannelExchangeState CHANNEL_EXCHANGE_OPEN = ChannelExchangeState__new(&ChannelExchangeState__open);
const ChannelExchangeState CHANNEL_EXCHANGE_SUBMITTING = ChannelExchangeState__new(&ChannelExchangeState__submitting);
const ChannelExchangeState CHANNEL_EXCHANGE_WORKING = ChannelExchangeState__new(&ChannelExchangeState__working);
const ChannelExchangeState CHANNEL_EXCHANGE_WORKING_CLIENT_CLOSED =
    ChannelExchangeState__new(&ChannelExchangeState__working_client_closed);
const ChannelExchangeState CHANNEL_EXCHANGE_CLOSING_SERVER =
    ChannelExchangeState__new(&ChannelExchangeState__closing_server);
const ChannelExchangeState CHANNEL_EXCHANGE_DELIVERING_REPLY =
    ChannelExchangeState__new(&ChannelExchangeState__delivering_reply);
const ChannelExchangeState CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE =
    ChannelExchangeState__new(&ChannelExchangeState__wait_client_close);
const ChannelExchangeState CHANNEL_EXCHANGE_RELEASING = ChannelExchangeState__new(&ChannelExchangeState__releasing);
const ChannelExchangeState CHANNEL_EXCHANGE_COMPLETE = ChannelExchangeState__new(&ChannelExchangeState__complete);

FixedArrayBodyDefine(ChannelExchanges);

ChannelExchange ChannelExchange__new(AtomicMessageQueuePtr client)
{
    return (ChannelExchange){.client = *client,
                             .server = None(AtomicMessageQueue),
                             .reply = None(ServiceQueueMessage),
                             .state = &CHANNEL_EXCHANGE_WAIT_OPEN};
}

ConstChannelExchangeStatePtr ChannelExchange__state(ChannelExchangePtr self)
{
    return self->state;
}

bool ChannelExchange__equals(void * first, void * second)
{
    ChannelExchangePtr a = first;
    ChannelExchangePtr b = second;
    return a->client.connection == b->client.connection;
}

static void ChannelExchange__reply(ChannelExchangePtr self, uint64_t kind)
{
    self->reply =
        Some(ServiceQueueMessage, ChannelExchange__message(kind, self->address, self->length, self->client.connection));
}

static void ChannelExchange__fail(ChannelExchangePtr self)
{
    if (!self->client_closed)
    {
        ChannelExchange__reply(self, CHANNEL_RESPONSE_FAILED);
    }
}

static bool ChannelExchange__client_disconnected(ChannelExchangePtr self)
{
    return !self->client_closed && AtomicMessageQueue__status(&self->client) == ATOMIC_MESSAGE_QUEUE_CLOSED;
}

static bool ChannelExchange__server_disconnected(ChannelExchangePtr self)
{
    match_optional(
        self->server,
        some(server) { return AtomicMessageQueue__status(&server) == ATOMIC_MESSAGE_QUEUE_CLOSED; },
        none_do_nothing()
    );
    return false;
}

static Result(ConstChannelExchangeStatePtr)
    ChannelExchange__close_client_then(ChannelExchangePtr self, ConstChannelExchangeStatePtr next)
{
    if (self->client_closed)
    {
        return Success(ConstChannelExchangeStatePtr, next);
    }
    AtomicMessageQueueCode code = atomic_message_queue_close(self->client.connection);
    if (code != ATOMIC_MESSAGE_QUEUE_OK)
    {
        return Error(ConstChannelExchangeStatePtr, bstr("Could not close channel client"), code);
    }
    self->client_closed = true;
    self->reply = None(ServiceQueueMessage);
    return Success(ConstChannelExchangeStatePtr, next);
}

static Result(ConstChannelExchangeStatePtr)
    ChannelExchange__close_disconnected_client_then(ChannelExchangePtr self, ConstChannelExchangeStatePtr current,
                                                    ConstChannelExchangeStatePtr next)
{
    if (!ChannelExchange__client_disconnected(self))
    {
        return Success(ConstChannelExchangeStatePtr, current);
    }
    return ChannelExchange__close_client_then(self, next);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__wait_open(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
    }
    match_result(
        AtomicMessageQueue__pop(&self->client),
        success(request)
        {
            if (request.kind != CHANNEL_REQUEST_OPEN)
            {
                return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
            }
            match_result(
                atomic_message_queue_connect(request.arg0),
                success(server)
                {
                    self->server = Some(AtomicMessageQueue, server);
                    if (server.peer_pid == self->client.peer_pid || server.peer_pid == process_current_pid())
                    {
                        ChannelExchange__fail(self);
                        return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_CLOSING_SERVER);
                    }
                    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_CONNECTING);
                },
                error(code, message) { return Error(ConstChannelExchangeStatePtr, message, code); }
            );
        },
        error(code, message)
        {
            if (code == ATOMIC_MESSAGE_QUEUE_EMPTY)
            {
                return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_WAIT_OPEN);
            }
            return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
        }
    );
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__connecting(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
    }
    match_optional(
        self->server,
        some(server)
        {
            AtomicMessageQueueCode status = AtomicMessageQueue__status(&server);
            if (status == ATOMIC_MESSAGE_QUEUE_PENDING)
            {
                return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_CONNECTING);
            }
            if (status != ATOMIC_MESSAGE_QUEUE_OK)
            {
                return Error(ConstChannelExchangeStatePtr, bstr("Channel server connection failed"), status);
            }
            match_result(
                mem_alloc_shared_page(self->client.peer_pid, server.peer_pid),
                success(address)
                {
                    self->address = (uint64_t)address;
                    ChannelExchange__reply(self, CHANNEL_RESPONSE_OPEN);
                    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_DELIVERING_OPEN);
                },
                error(code, message) { return Error(ConstChannelExchangeStatePtr, message, code); }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelExchangeStatePtr, bstr("Channel server connection is missing"),
                 ATOMIC_MESSAGE_QUEUE_INVALID);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__delivering_open(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
    }
    if (ChannelExchange__server_disconnected(self))
    {
        return Error(ConstChannelExchangeStatePtr, bstr("Channel server disconnected"), ATOMIC_MESSAGE_QUEUE_CLOSED);
    }
    match_optional(
        self->reply,
        some(reply)
        {
            match_result(
                AtomicMessageQueue__push(&self->client, &reply),
                success(status)
                {
                    self->reply = None(ServiceQueueMessage);
                    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_OPEN);
                },
                error(code, message)
                {
                    if (code == ATOMIC_MESSAGE_QUEUE_FULL)
                    {
                        return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_DELIVERING_OPEN);
                    }
                    return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelExchangeStatePtr, bstr("Channel open reply is missing"), ATOMIC_MESSAGE_QUEUE_INVALID);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__open(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
    }
    if (ChannelExchange__server_disconnected(self))
    {
        return Error(ConstChannelExchangeStatePtr, bstr("Channel server disconnected"), ATOMIC_MESSAGE_QUEUE_CLOSED);
    }
    match_result(
        AtomicMessageQueue__pop(&self->client),
        success(request)
        {
            if (request.kind != CHANNEL_REQUEST_SUBMIT || request.arg2 != self->client.connection ||
                request.arg1 >= CHANNEL_EXCHANGE_CAPACITY)
            {
                return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
            }
            self->length = request.arg1;
            return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_SUBMITTING);
        },
        error(code, message)
        {
            if (code == ATOMIC_MESSAGE_QUEUE_EMPTY)
            {
                return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_OPEN);
            }
            return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
        }
    );
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__submitting(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_CLOSING_SERVER);
    }
    ServiceQueueMessage request =
        ChannelExchange__message(CHANNEL_REQUEST_SERVE, self->address, self->length, self->client.connection);
    match_optional(
        self->server,
        some(server)
        {
            match_result(
                AtomicMessageQueue__push(&server, &request),
                success(status)
                {
                    self->server = Some(AtomicMessageQueue, server);
                    return ChannelExchangeState__working(self);
                },
                error(code, message)
                {
                    self->server = Some(AtomicMessageQueue, server);
                    if (code == ATOMIC_MESSAGE_QUEUE_FULL)
                    {
                        return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_SUBMITTING);
                    }
                    return Error(ConstChannelExchangeStatePtr, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelExchangeStatePtr, bstr("Channel server connection is missing"),
                 ATOMIC_MESSAGE_QUEUE_INVALID);
}

static Result(ServiceQueueMessage) ChannelExchange__receive_completion(ChannelExchangePtr self)
{
    match_optional(
        self->server,
        some(server)
        {
            match_result(
                AtomicMessageQueue__pop(&server),
                success(reply)
                {
                    self->server = Some(AtomicMessageQueue, server);
                    return Success(ServiceQueueMessage, reply);
                },
                error(code, message)
                {
                    self->server = Some(AtomicMessageQueue, server);
                    return Error(ServiceQueueMessage, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ServiceQueueMessage, bstr("Channel server connection is missing"), ATOMIC_MESSAGE_QUEUE_INVALID);
}

static bool ChannelExchange__valid_completion(ChannelExchangePtr self, ConstServiceQueueMessagePtr reply)
{
    return reply->kind == CHANNEL_RESPONSE_REPLIED && reply->arg2 == self->client.connection &&
           reply->arg1 == self->length;
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__working(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_WORKING_CLIENT_CLOSED);
    }
    match_result(
        ChannelExchange__receive_completion(self),
        success(reply)
        {
            if (!ChannelExchange__valid_completion(self, &reply))
            {
                return Error(ConstChannelExchangeStatePtr, bstr("Invalid channel server reply"),
                             ATOMIC_MESSAGE_QUEUE_INVALID);
            }
            ChannelExchange__reply(self, CHANNEL_RESPONSE_REPLIED);
            return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_CLOSING_SERVER);
        },
        error(code, message)
        {
            if (code == ATOMIC_MESSAGE_QUEUE_EMPTY)
            {
                return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_WORKING);
            }
            return Error(ConstChannelExchangeStatePtr, message, code);
        }
    );
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__working_client_closed(ChannelExchangePtr self)
{
    match_result(
        ChannelExchange__receive_completion(self),
        success(reply) { return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_CLOSING_SERVER); },
        error(code, message)
        {
            if (code == ATOMIC_MESSAGE_QUEUE_EMPTY)
            {
                return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_WORKING_CLIENT_CLOSED);
            }
            return Error(ConstChannelExchangeStatePtr, message, code);
        }
    );
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__closing_server(ChannelExchangePtr self)
{
    match_optional(
        self->server,
        some(server)
        {
            AtomicMessageQueueCode code = atomic_message_queue_close(server.connection);
            if (code != ATOMIC_MESSAGE_QUEUE_OK)
            {
                return Error(ConstChannelExchangeStatePtr, bstr("Could not close channel server"), code);
            }
            self->server = None(AtomicMessageQueue);
        },
        none_do_nothing()
    );
    if (self->client_closed)
    {
        return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_RELEASING);
    }
    match_optional(
        self->reply,
        some(reply) { return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_DELIVERING_REPLY); },
        none_do_nothing()
    );
    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__delivering_reply(ChannelExchangePtr self)
{
    if (ChannelExchange__client_disconnected(self))
    {
        return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_RELEASING);
    }
    match_optional(
        self->reply,
        some(reply)
        {
            match_result(
                AtomicMessageQueue__push(&self->client, &reply),
                success(status)
                {
                    self->reply = None(ServiceQueueMessage);
                    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE);
                },
                error(code, message)
                {
                    if (code == ATOMIC_MESSAGE_QUEUE_FULL)
                    {
                        return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_DELIVERING_REPLY);
                    }
                    return ChannelExchange__close_client_then(self, &CHANNEL_EXCHANGE_RELEASING);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelExchangeStatePtr, bstr("Channel reply is missing"), ATOMIC_MESSAGE_QUEUE_INVALID);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__wait_client_close(ChannelExchangePtr self)
{
    return ChannelExchange__close_disconnected_client_then(self, &CHANNEL_EXCHANGE_WAIT_CLIENT_CLOSE,
                                                           &CHANNEL_EXCHANGE_RELEASING);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__releasing(ChannelExchangePtr self)
{
    if (self->address == 0)
    {
        return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_COMPLETE);
    }
    MemStatus code = mem_release_shared_page(self->address);
    if (code != MEM_OK)
    {
        return Error(ConstChannelExchangeStatePtr, bstr("Could not release channel payload"), code);
    }
    self->address = 0;
    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_COMPLETE);
}

static Result(ConstChannelExchangeStatePtr) ChannelExchangeState__complete(ChannelExchangePtr self)
{
    return Success(ConstChannelExchangeStatePtr, &CHANNEL_EXCHANGE_COMPLETE);
}

static ConstChannelExchangeStatePtr ChannelExchange__recover(ChannelExchangePtr self)
{
    if (self->state == &CHANNEL_EXCHANGE_CLOSING_SERVER || self->state == &CHANNEL_EXCHANGE_RELEASING)
    {
        return self->state;
    }
    ChannelExchange__fail(self);
    return &CHANNEL_EXCHANGE_CLOSING_SERVER;
}

bool ChannelExchange__step(ChannelExchangePtr self)
{
    ConstChannelExchangeStatePtr current = self->state;
    match_result(
        current->action(self),
        success(next)
        {
            self->state = next;
            return next != current;
        },
        error(code, message)
        {
            ConstChannelExchangeStatePtr next = ChannelExchange__recover(self);
            self->state = next;
            return next != current;
        }
    );
}

static bool ChannelExchange__matches_connection(ChannelExchangePtr self, void * context)
{
    return self->client.connection == *(AtomicMessageQueueConnectionID *)context;
}

bool ChannelExchanges__contains(ChannelExchangesPtr self, AtomicMessageQueueConnectionID connection)
{
    match_find_result(
        ChannelExchanges__find(self, ChannelExchange__matches_connection, &connection),
        some(exchange) { return true; },
        none_do_nothing()
    );
    return false;
}

static void ChannelExchanges__accept_connection(ChannelExchangesPtr self, AtomicMessageQueueConnectionID connection)
{
    match_result(
        atomic_message_queue_accept(connection),
        success(queue)
        {
            ChannelExchange exchange = ChannelExchange__new(&queue);
            match_result(
                ChannelExchanges__push(self, exchange),
                success_do_nothing(),
                error(code, message) { atomic_message_queue_close(connection); }
            );
        },
        error(code, message) { atomic_message_queue_close(connection); }
    );
}

bool ChannelExchanges__step(ChannelExchangesPtr self, AtomicMessageQueueArrivalsPtr arrivals)
{
    bool work = false;
    for_each_atomic_fa_ptr(AtomicMessageQueueConnectionSlots, &arrivals->connections, index, id, {
        if (id == 0 || ChannelExchanges__contains(self, id))
        {
            continue;
        }
        ChannelExchanges__accept_connection(self, id);
        work = true;
    });
    for_each_fa_ptr(ChannelExchanges, self, index, value, {
        match_optional(
            ChannelExchanges__ptr_at(self, index),
            some(exchange)
            {
                work |= ChannelExchange__step(exchange);
                if (ChannelExchange__state(exchange) != &CHANNEL_EXCHANGE_COMPLETE)
                {
                    continue;
                }
                ChannelExchanges__remove(self, value);
                work = true;
                break;
            },
            none_do_nothing()
        );
    });
    return work;
}

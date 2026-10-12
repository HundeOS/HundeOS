#include "lifecycle.h"
#include "ipc/io_diagnostics.h"

#include "sys/mem.h"
#include "sys/process.h"

#define ChannelSessionState__new(state_action) {.action = state_action}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__wait_open(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__connecting(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__delivering_open(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__open(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__submitting(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__working(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__cancelling(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__working_client_closed(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__closing_server(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__delivering_reply(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__wait_client_acknowledgement(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__wait_client_close(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__releasing(ChannelSessionPtr self);
static Result(ConstChannelSessionStatePtr) ChannelSessionState__complete(ChannelSessionPtr self);
static bool ChannelSession__matches_connection(ChannelSessionPtr self, void * context);

const ChannelSessionState CHANNEL_SESSION_WAIT_OPEN = ChannelSessionState__new(&ChannelSessionState__wait_open);
const ChannelSessionState CHANNEL_SESSION_CONNECTING = ChannelSessionState__new(&ChannelSessionState__connecting);
const ChannelSessionState CHANNEL_SESSION_DELIVERING_OPEN =
    ChannelSessionState__new(&ChannelSessionState__delivering_open);
const ChannelSessionState CHANNEL_SESSION_OPEN = ChannelSessionState__new(&ChannelSessionState__open);
const ChannelSessionState CHANNEL_SESSION_SUBMITTING = ChannelSessionState__new(&ChannelSessionState__submitting);
const ChannelSessionState CHANNEL_SESSION_WORKING = ChannelSessionState__new(&ChannelSessionState__working);
const ChannelSessionState CHANNEL_SESSION_CANCELLING = ChannelSessionState__new(&ChannelSessionState__cancelling);
const ChannelSessionState CHANNEL_SESSION_WORKING_CLIENT_CLOSED =
    ChannelSessionState__new(&ChannelSessionState__working_client_closed);
const ChannelSessionState CHANNEL_SESSION_CLOSING_SERVER =
    ChannelSessionState__new(&ChannelSessionState__closing_server);
const ChannelSessionState CHANNEL_SESSION_DELIVERING_REPLY =
    ChannelSessionState__new(&ChannelSessionState__delivering_reply);
const ChannelSessionState CHANNEL_SESSION_WAIT_CLIENT_ACKNOWLEDGEMENT =
    ChannelSessionState__new(&ChannelSessionState__wait_client_acknowledgement);
const ChannelSessionState CHANNEL_SESSION_WAIT_CLIENT_CLOSE =
    ChannelSessionState__new(&ChannelSessionState__wait_client_close);
const ChannelSessionState CHANNEL_SESSION_RELEASING = ChannelSessionState__new(&ChannelSessionState__releasing);
const ChannelSessionState CHANNEL_SESSION_COMPLETE = ChannelSessionState__new(&ChannelSessionState__complete);

FixedArrayBodyDefine(ChannelSessions);

ChannelSession ChannelSession__new(QueuePtr client)
{
    return (ChannelSession){
        .client = *client, .server = None(Queue), .reply = None(QueueMessage), .state = &CHANNEL_SESSION_WAIT_OPEN};
}

ConstChannelSessionStatePtr ChannelSession__state(ChannelSessionPtr self)
{
    return self->state;
}

bool ChannelSession__equals(void * first, void * second)
{
    ChannelSessionPtr a = first;
    ChannelSessionPtr b = second;
    return Queue__connection(&a->client) == Queue__connection(&b->client);
}

static void ChannelSession__reply(ChannelSessionPtr self, uint64_t kind)
{
    QueueMessage reply =
        ChannelControlMessage__new(kind, self->address, self->length, Queue__connection(&self->client));
    reply.request = self->ticket.metadata;
    self->reply = Some(QueueMessage, reply);
}

static void ChannelSession__fail(ChannelSessionPtr self)
{
    if (!self->client_closed)
    {
        ChannelSession__reply(self, CHANNEL_RESPONSE_FAILED);
    }
}

static bool ChannelSession__client_disconnected(ChannelSessionPtr self)
{
    return !self->client_closed && Queue__status(&self->client) == QUEUE_CLOSED;
}

static bool ChannelSession__server_disconnected(ChannelSessionPtr self)
{
    match_optional(self->server,
        some(server)
        {
            return Queue__status(&server) == QUEUE_CLOSED;
        },
        none_do_nothing()
    );
    return false;
}

static Result(ConstChannelSessionStatePtr)
    ChannelSession__close_client_then(ChannelSessionPtr self, ConstChannelSessionStatePtr next)
{
    if (self->client_closed)
    {
        return Success(ConstChannelSessionStatePtr, next);
    }
    QueueCode code = QueueConnection__close(Queue__connection(&self->client));
    if (code != QUEUE_OK)
    {
        return Error(ConstChannelSessionStatePtr, bstr("Could not close channel client"), code);
    }
    self->client_closed = true;
    self->reply = None(QueueMessage);
    return Success(ConstChannelSessionStatePtr, next);
}

static Result(ConstChannelSessionStatePtr)
    ChannelSession__close_disconnected_client_then(ChannelSessionPtr self, ConstChannelSessionStatePtr current,
                                                   ConstChannelSessionStatePtr next)
{
    if (!ChannelSession__client_disconnected(self))
    {
        return Success(ConstChannelSessionStatePtr, current);
    }
    return ChannelSession__close_client_then(self, next);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__wait_open(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    match_result(
        Queue__pop(&self->client),
        success(request)
        {
            if (request.kind != CHANNEL_REQUEST_OPEN)
            {
                return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
            }
            match_result(
                Queue__connect(request.arg0),
                success(server)
                {
                    self->server = Some(Queue, server);
                    if (Queue__peer_pid(&server) == Queue__peer_pid(&self->client) ||
                        Queue__peer_pid(&server) == process_current_pid())
                    {
                        ChannelSession__fail(self);
                        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_CLOSING_SERVER);
                    }
                    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_CONNECTING);
                },
                error(code, message)
                {
                    return Error(ConstChannelSessionStatePtr, message, code);
                }
            );
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY)
            {
                return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WAIT_OPEN);
            }
            return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
        }
    );
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__connecting(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    match_optional(
        self->server,
        some(server)
        {
            QueueCode status = Queue__status(&server);
            if (status == QUEUE_PENDING)
            {
                return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_CONNECTING);
            }
            if (status != QUEUE_OK)
            {
                return Error(ConstChannelSessionStatePtr, bstr("Channel server connection failed"), status);
            }
            match_result(
                mem_alloc_shared_page(Queue__peer_pid(&self->client), Queue__peer_pid(&server)),
                success(address)
                {
                    self->address = (uint64_t)address;
                    ChannelSession__reply(self, CHANNEL_RESPONSE_OPEN);
                    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_DELIVERING_OPEN);
                },
                error(code, message)
                {
                    return Error(ConstChannelSessionStatePtr, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelSessionStatePtr, bstr("Channel server connection is missing"), QUEUE_INVALID);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__delivering_open(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    if (ChannelSession__server_disconnected(self))
    {
        return Error(ConstChannelSessionStatePtr, bstr("Channel server disconnected"), QUEUE_CLOSED);
    }
    match_optional(
        self->reply,
        some(reply)
        {
            match_result(
                Queue__push(&self->client, &reply),
                success(status)
                {
                    self->reply = None(QueueMessage);
                    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_OPEN);
                },
                error(code, message)
                {
                    if (code == QUEUE_FULL)
                    {
                        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_DELIVERING_OPEN);
                    }
                    return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelSessionStatePtr, bstr("Channel open reply is missing"), QUEUE_INVALID);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__open(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    if (ChannelSession__server_disconnected(self))
    {
        return Error(ConstChannelSessionStatePtr, bstr("Channel server disconnected"), QUEUE_CLOSED);
    }
    match_result(
        Queue__pop(&self->client),
        success(request)
        {
            if (request.kind != CHANNEL_REQUEST_SUBMIT || request.arg2 != Queue__connection(&self->client) ||
                request.arg1 >= CHANNEL_PAYLOAD_CAPACITY || request.arg3 != 0)
            {
                return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
            }
            RequestTicket ticket = ChannelControlMessage__ticket(&request);
            if (!RequestTicket__valid(&ticket) || ticket.metadata.generation <= self->ticket.metadata.generation ||
                (ticket.metadata.parent_connection == 0 &&
                 ticket.metadata.origin_pid != Queue__origin_pid(&self->client)))
            {
                return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
            }
            self->ticket = ticket;
            self->length = request.arg1;
            return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_SUBMITTING);
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY)
            {
                return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_OPEN);
            }
            return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
        }
    );
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__submitting(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    QueueMessage request = ChannelControlMessage__new(CHANNEL_REQUEST_SERVE, self->address, self->length,
                                                      Queue__connection(&self->client));
    request.arg3 = Queue__peer_pid(&self->client);
    request.request = self->ticket.metadata;
    match_optional(
        self->server,
        some(server)
        {
            match_result(
                Queue__push(&server, &request),
                success(status)
                {
                    self->server = Some(Queue, server);
                    IODiagnostics__record(IO_DIAGNOSTIC_WARDEN_RELAYS, 1);
                    return ChannelSessionState__working(self);
                },
                error(code, message)
                {
                    self->server = Some(Queue, server);
                    if (code == QUEUE_FULL)
                    {
                        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_SUBMITTING);
                    }
                    return Error(ConstChannelSessionStatePtr, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelSessionStatePtr, bstr("Channel server connection is missing"), QUEUE_INVALID);
}

static Result(QueueMessage) ChannelSession__receive_completion(ChannelSessionPtr self)
{
    match_optional(
        self->server,
        some(server)
        {
            match_result(
                Queue__pop(&server),
                success(reply)
                {
                    self->server = Some(Queue, server);
                    return Success(QueueMessage, reply);
                },
                error(code, message)
                {
                    self->server = Some(Queue, server);
                    return Error(QueueMessage, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(QueueMessage, bstr("Channel server connection is missing"), QUEUE_INVALID);
}

static bool ChannelSession__valid_completion(ChannelSessionPtr self, ConstQueueMessagePtr reply)
{
    RequestTicket ticket = ChannelControlMessage__ticket(reply);
    return reply->kind == CHANNEL_RESPONSE_REPLIED && reply->arg2 == Queue__connection(&self->client) &&
           reply->arg1 < CHANNEL_PAYLOAD_CAPACITY && RequestTicket__equals(&self->ticket, &ticket);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__working(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CANCELLING);
    }
    match_result(
        ChannelSession__receive_completion(self),
        success(reply)
        {
            if (!ChannelSession__valid_completion(self, &reply))
            {
                return Error(ConstChannelSessionStatePtr, bstr("Invalid channel server reply"), QUEUE_INVALID);
            }
            self->length = reply.arg1;
            ChannelSession__reply(self, CHANNEL_RESPONSE_REPLIED);
            return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_DELIVERING_REPLY);
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY)
            {
                return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WORKING);
            }
            return Error(ConstChannelSessionStatePtr, message, code);
        }
    );
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__cancelling(ChannelSessionPtr self)
{
    match_optional(
        self->server,
        some(server)
        {
            QueueMessage cancellation =
                ChannelControlMessage__new(CHANNEL_REQUEST_CANCEL, 0, 0, Queue__connection(&self->client));
            cancellation.request = self->ticket.metadata;
            match_result(
                Queue__push(&server, &cancellation),
                success(code)
                {
                    self->server = Some(Queue, server);
                    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WORKING_CLIENT_CLOSED);
                },
                error(code, message)
                {
                    self->server = Some(Queue, server);
                    if (code == QUEUE_FULL)
                    {
                        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_CANCELLING);
                    }
                    return Error(ConstChannelSessionStatePtr, message, code);
                }
            );
        },
        none()
        {
            return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_CLOSING_SERVER);
        }
    );
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__working_client_closed(ChannelSessionPtr self)
{
    match_result(
        ChannelSession__receive_completion(self),
        success(reply)
        {
            return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_CLOSING_SERVER);
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY)
            {
                return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WORKING_CLIENT_CLOSED);
            }
            return Error(ConstChannelSessionStatePtr, message, code);
        }
    );
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__closing_server(ChannelSessionPtr self)
{
    match_optional(
        self->server,
        some(server)
        {
            QueueCode code = QueueConnection__close(Queue__connection(&server));
            if (code != QUEUE_OK)
            {
                return Error(ConstChannelSessionStatePtr, bstr("Could not close channel server"), code);
            }
            self->server = None(Queue);
        },
        none_do_nothing()
    );
    if (self->client_closed)
    {
        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_RELEASING);
    }
    match_optional(
        self->reply,
        some(reply)
        {
            return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_DELIVERING_REPLY);
        },
        none_do_nothing()
    );
    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WAIT_CLIENT_CLOSE);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__delivering_reply(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    match_optional(
        self->reply,
        some(reply)
        {
            match_result(
                Queue__push(&self->client, &reply),
                success(status)
                {
                    self->reply = None(QueueMessage);
                    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WAIT_CLIENT_ACKNOWLEDGEMENT);
                },
                error(code, message)
                {
                    if (code == QUEUE_FULL)
                    {
                        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_DELIVERING_REPLY);
                    }
                    return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
                }
            );
        },
        none_do_nothing()
    );
    return Error(ConstChannelSessionStatePtr, bstr("Channel reply is missing"), QUEUE_INVALID);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__wait_client_acknowledgement(ChannelSessionPtr self)
{
    if (ChannelSession__client_disconnected(self))
    {
        return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
    }
    if (ChannelSession__server_disconnected(self))
    {
        return Error(ConstChannelSessionStatePtr, bstr("Channel server disconnected"), QUEUE_CLOSED);
    }
    match_result(
        Queue__pop(&self->client),
        success(request)
        {
            RequestTicket ticket = ChannelControlMessage__ticket(&request);
            if (request.kind != CHANNEL_REQUEST_ACKNOWLEDGE || request.arg2 != Queue__connection(&self->client) ||
                !RequestTicket__equals(&self->ticket, &ticket))
            {
                return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
            }
            self->length = 0;
            return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_OPEN);
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY)
            {
                return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_WAIT_CLIENT_ACKNOWLEDGEMENT);
            }
            return ChannelSession__close_client_then(self, &CHANNEL_SESSION_CLOSING_SERVER);
        }
    );
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__wait_client_close(ChannelSessionPtr self)
{
    return ChannelSession__close_disconnected_client_then(self, &CHANNEL_SESSION_WAIT_CLIENT_CLOSE,
                                                          &CHANNEL_SESSION_RELEASING);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__releasing(ChannelSessionPtr self)
{
    if (self->address == 0)
    {
        return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_COMPLETE);
    }
    MemStatus code = mem_release_shared_page(self->address);
    if (code != MEM_OK)
    {
        return Error(ConstChannelSessionStatePtr, bstr("Could not release channel payload"), code);
    }
    self->address = 0;
    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_COMPLETE);
}

static Result(ConstChannelSessionStatePtr) ChannelSessionState__complete(ChannelSessionPtr self)
{
    return Success(ConstChannelSessionStatePtr, &CHANNEL_SESSION_COMPLETE);
}

static ConstChannelSessionStatePtr ChannelSession__recover(ChannelSessionPtr self)
{
    if (self->state == &CHANNEL_SESSION_CLOSING_SERVER || self->state == &CHANNEL_SESSION_RELEASING)
    {
        return self->state;
    }
    ChannelSession__fail(self);
    return &CHANNEL_SESSION_CLOSING_SERVER;
}

bool ChannelSession__step(ChannelSessionPtr self)
{
    ConstChannelSessionStatePtr current = self->state;
    match_result(
        current->action(self),
        success(next)
        {
            self->state = next;
            return next != current;
        },
        error(code, message)
        {
            ConstChannelSessionStatePtr next = ChannelSession__recover(self);
            self->state = next;
            return next != current;
        }
    );
}

static bool ChannelSession__matches_connection(ChannelSessionPtr self, void * context)
{
    return Queue__connection(&self->client) == *(QueueConnectionIDPtr)context;
}

bool ChannelSessions__contains(ChannelSessionsPtr self, QueueConnectionID connection)
{
    match_find_result(
        ChannelSessions__find(self, ChannelSession__matches_connection, &connection),
        some(exchange)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static void ChannelSessions__accept_connection(ChannelSessionsPtr self, QueueConnectionID connection)
{
    match_result(
        Queue__accept(connection),
        success(queue)
        {
            ChannelSession exchange = ChannelSession__new(&queue);
            match_result(
                ChannelSessions__push(self, exchange),
                success_do_nothing(),
                error(code, message)
                {
                    QueueConnection__close(connection);
                }
            );
        },
        error(code, message)
        {
            QueueConnection__close(connection);
        }
    );
}

bool ChannelSessions__step(ChannelSessionsPtr self, QueueArrivalsPtr arrivals)
{
    bool work = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &arrivals->connections,
        index,
        id,
        {
            if (id == 0 || ChannelSessions__contains(self, id))
            {
                continue;
            }
            ChannelSessions__accept_connection(self, id);
            work = true;
        }
    );
    for_each_fa_ptr(
        ChannelSessions,
        self,
        index,
        value,
        {
            match_optional(
                ChannelSessions__ptr_at(self, index),
                some(exchange)
                {
                    work |= ChannelSession__step(exchange);
                    if (ChannelSession__state(exchange) != &CHANNEL_SESSION_COMPLETE)
                    {
                        continue;
                    }
                    ChannelSessions__remove(self, value);
                    work = true;
                    break;
                },
                none_do_nothing()
            );
        }
    );
    return work;
}

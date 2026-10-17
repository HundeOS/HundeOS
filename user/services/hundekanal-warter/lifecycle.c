#include "lifecycle.h"

FixedArrayBodyDefine(ChannelSessions);

ChannelSession ChannelSession__new(QueuePtr client)
{
    return (ChannelSession){
        .client = *client,
        .reply = None(QueueMessage),
        .delegated = 0,
        .state = CHANNEL_SESSION_WAIT_OPEN,
    };
}

ChannelSessionState ChannelSession__state(ChannelSessionPtr self)
{
    return self->state;
}

bool ChannelSession__equals(void * first, void * second)
{
    ChannelSessionPtr a = first;
    ChannelSessionPtr b = second;
    return Queue__connection(&a->client) == Queue__connection(&b->client);
}

static void ChannelSession__prepare_reply(ChannelSessionPtr self, uint64_t kind, QueueConnectionID connection)
{
    self->reply = Some(QueueMessage, ChannelControlMessage__new(kind, connection, 0, Queue__connection(&self->client)));
    self->state = CHANNEL_SESSION_DELIVERING_OPEN;
}

static bool ChannelSession__wait_open(ChannelSessionPtr self)
{
    if (Queue__status(&self->client) == QUEUE_CLOSED)
    {
        self->state = CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
        return true;
    }
    match_result(
        Queue__pop(&self->client),
        success(request)
        {
            if (request.kind != CHANNEL_REQUEST_OPEN || request.arg0 == 0 ||
                request.arg2 != Queue__connection(&self->client))
            {
                ChannelSession__prepare_reply(self, CHANNEL_RESPONSE_FAILED, 0);
                return true;
            }
            match_result(
                Queue__delegate(Queue__connection(&self->client), request.arg0),
                success(connection)
                {
                    self->delegated = connection;
                    ChannelSession__prepare_reply(self, CHANNEL_RESPONSE_OPEN, connection);
                },
                error(code, message)
                {
                    ChannelSession__prepare_reply(self, CHANNEL_RESPONSE_FAILED, 0);
                }
            );
            return true;
        },
        error(code, message)
        {
            if (code != QUEUE_EMPTY && code != QUEUE_PENDING)
            {
                self->state = CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
                return true;
            }
        }
    );
    return false;
}

static bool ChannelSession__deliver_open(ChannelSessionPtr self)
{
    if (Queue__status(&self->client) == QUEUE_CLOSED)
    {
        if (self->delegated != 0)
        {
            Queue__cancel_delegation(Queue__connection(&self->client), self->delegated);
            self->delegated = 0;
        }
        self->state = CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
        return true;
    }
    match_optional(
        self->reply,
        some(reply)
        {
            match_result(
                Queue__push(&self->client, &reply),
                success(code)
                {
                    self->reply = None(QueueMessage);
                    self->state = CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
                    return true;
                },
                error(code, message)
                {
                    if (code != QUEUE_FULL && code != QUEUE_PENDING)
                    {
                        self->state = CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
                        return true;
                    }
                }
            );
        },
        none()
        {
            self->state = CHANNEL_SESSION_WAIT_CLIENT_CLOSE;
            return true;
        }
    );
    return false;
}

static bool ChannelSession__wait_client_close(ChannelSessionPtr self)
{
    if (Queue__status(&self->client) != QUEUE_CLOSED)
    {
        return false;
    }
    if (!self->client_closed)
    {
        QueueProgress progress = Queue__progress(&self->client);
        if (self->delegated != 0 && progress.incoming_consumed != progress.outgoing_produced)
        {
            Queue__cancel_delegation(Queue__connection(&self->client), self->delegated);
        }
        self->delegated = 0;
        self->client_closed = QueueConnection__close(Queue__connection(&self->client)) == QUEUE_OK;
        if (!self->client_closed)
        {
            return false;
        }
    }
    self->state = CHANNEL_SESSION_COMPLETE;
    return true;
}

bool ChannelSession__step(ChannelSessionPtr self)
{
    if (self->state == CHANNEL_SESSION_WAIT_OPEN)
    {
        return ChannelSession__wait_open(self);
    }
    if (self->state == CHANNEL_SESSION_DELIVERING_OPEN)
    {
        return ChannelSession__deliver_open(self);
    }
    if (self->state == CHANNEL_SESSION_WAIT_CLIENT_CLOSE)
    {
        return ChannelSession__wait_client_close(self);
    }
    return false;
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
            match_result(
                ChannelSessions__push(self, ChannelSession__new(&queue)),
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
            if (id != 0 && !ChannelSessions__contains(self, id))
            {
                ChannelSessions__accept_connection(self, id);
                work = true;
            }
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
                    if (ChannelSession__state(exchange) == CHANNEL_SESSION_COMPLETE)
                    {
                        ChannelSessions__remove(self, value);
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

#include "queue.h"
#include "ipc_platform.h"

static BString queue_unavailable = bstr("Queue unavailable");
static BString queue_invalid_identity = bstr("Invalid queue identity");
static BString queue_corrupt_cursor = bstr("Corrupt queue cursor");
static BString queue_full = bstr("Queue full");
static BString queue_empty = bstr("Queue empty");

static QueuePublication QueuePublication__new(ConstQueueMessagePtr message, QueueCursor cursor)
{
    return (QueuePublication){.message = *message, .cursor = cursor};
}

static QueueProgress QueueProgress__new(QueueCursor incoming_produced, QueueCursor incoming_consumed,
                                        QueueCursor outgoing_produced, QueueCursor outgoing_consumed,
                                        QueueStateFlag accepted, QueueStateFlag closed)
{
    return (QueueProgress){
        .incoming_produced = incoming_produced,
        .incoming_consumed = incoming_consumed,
        .outgoing_produced = outgoing_produced,
        .outgoing_consumed = outgoing_consumed,
        .accepted = accepted,
        .closed = closed,
    };
}

Queue Queue__new(QueueIdentityPtr identity, QueuePagePtr outgoing, QueuePagePtr incoming, QueueConnectionID connection,
                 QueueParticipantID peer_pid, IpcDoorbellPtr peer_doorbell, QueueDataNotifier notify_data_available)
{
    return (Queue){
        .identity = identity,
        .outgoing = outgoing,
        .incoming = incoming,
        .connection = connection,
        .peer_pid = peer_pid,
        .peer_doorbell = peer_doorbell,
        .notify_data_available = notify_data_available,
        .read = 0,
        .written = 0,
        .acknowledged = 0,
    };
}

Result(Queue)
    Queue__attach(QueueIdentityPtr identity, QueuePagePtr client, QueuePagePtr server, QueueConnectionID connection,
                  QueueParticipantID pid, IpcDoorbellPtr peer_doorbell, QueueDataNotifier notify_data_available)
{
    if (identity->connection != connection || (pid != identity->client_pid && pid != identity->server_pid))
    {
        return Error(Queue, queue_invalid_identity, QUEUE_INVALID);
    }
    bool is_client = pid == identity->client_pid;
    Queue queue =
        Queue__new(identity, is_client ? client : server, is_client ? server : client, connection,
                   is_client ? identity->server_pid : identity->client_pid, peer_doorbell, notify_data_available);
    return Success(Queue, queue);
}

void Queue__ignore_data_available(QueueConnectionID connection)
{
}

QueueCode Queue__status(QueuePtr self)
{
    if (__atomic_load_n(&self->identity->closed, __ATOMIC_ACQUIRE) != 0)
    {
        return QUEUE_CLOSED;
    }
    if (__atomic_load_n(&self->identity->accepted, __ATOMIC_ACQUIRE) == 0)
    {
        return QUEUE_PENDING;
    }
    return QUEUE_OK;
}

QueueConnectionID Queue__connection(QueuePtr self)
{
    return self->connection;
}

bool Queue__has_unread_messages(QueuePtr self)
{
    if (Queue__status(self) != QUEUE_OK)
    {
        return false;
    }
    // Each endpoint acknowledges incoming messages on its own outgoing page.
    QueueCursor consumed = __atomic_load_n(&self->outgoing->consumed, __ATOMIC_ACQUIRE);
    QueueCursor produced = __atomic_load_n(&self->incoming->produced, __ATOMIC_ACQUIRE);
    QueueMessageCount pending = produced - consumed;
    return pending != 0 && pending <= QUEUE_CAPACITY;
}

bool Queue__has_write_capacity(QueuePtr self)
{
    QueueCode status = Queue__status(self);
    if (status == QUEUE_CLOSED)
    {
        return true;
    }
    if (status != QUEUE_OK)
    {
        return false;
    }
    QueueCursor consumed = __atomic_load_n(&self->incoming->consumed, __ATOMIC_ACQUIRE);
    return self->written - consumed != QUEUE_CAPACITY;
}

QueueParticipantID Queue__peer_pid(QueuePtr self)
{
    return self->peer_pid;
}

QueueParticipantID Queue__local_pid(QueuePtr self)
{
    return self->peer_pid == self->identity->server_pid ? self->identity->client_pid : self->identity->server_pid;
}

QueueParticipantID Queue__origin_pid(QueuePtr self)
{
    return self->identity->origin_pid;
}

QueueProgress Queue__progress(QueuePtr self)
{
    return QueueProgress__new(__atomic_load_n(&self->incoming->produced, __ATOMIC_ACQUIRE),
                              __atomic_load_n(&self->incoming->consumed, __ATOMIC_ACQUIRE),
                              __atomic_load_n(&self->outgoing->produced, __ATOMIC_ACQUIRE),
                              __atomic_load_n(&self->outgoing->consumed, __ATOMIC_ACQUIRE),
                              __atomic_load_n(&self->identity->accepted, __ATOMIC_ACQUIRE),
                              __atomic_load_n(&self->identity->closed, __ATOMIC_ACQUIRE));
}

bool QueueProgress__equals(ConstQueueProgressPtr self, ConstQueueProgressPtr other)
{
    return self->incoming_produced == other->incoming_produced && self->incoming_consumed == other->incoming_consumed &&
           self->outgoing_produced == other->outgoing_produced && self->outgoing_consumed == other->outgoing_consumed &&
           self->accepted == other->accepted && self->closed == other->closed;
}

Result(QueueCode) Queue__push(QueuePtr self, ConstQueueMessagePtr message)
{
    QueueCode status = Queue__status(self);
    if (status != QUEUE_OK)
    {
        return Error(QueueCode, queue_unavailable, status);
    }
    QueueCursor consumed = __atomic_load_n(&self->incoming->consumed, __ATOMIC_ACQUIRE);
    if (consumed - self->acknowledged > self->written - self->acknowledged)
    {
        return Error(QueueCode, queue_corrupt_cursor, QUEUE_INVALID);
    }
    self->acknowledged = consumed;
    QueueMessageCount pending = self->written - consumed;
    if (pending > QUEUE_CAPACITY)
    {
        return Error(QueueCode, queue_corrupt_cursor, QUEUE_INVALID);
    }
    if (pending == QUEUE_CAPACITY)
    {
        return Error(QueueCode, queue_full, QUEUE_FULL);
    }
    self->outgoing->messages[self->written % QUEUE_CAPACITY] = *message;
    QueuePublication publication = QueuePublication__new(message, self->written + 1);
    match_result(
        QueuePublications__set(&self->outgoing->publication, 0, &publication),
        success_do_nothing(),
        error(code, error_message)
        {
            return Error(QueueCode, error_message, QUEUE_INVALID);
        }
    );
    self->written++;
    __atomic_store_n(&self->outgoing->produced, self->written, __ATOMIC_RELEASE);
    IpcEvent__ring(IpcDoorbell__mutable_event(self->peer_doorbell));
    IpcPlatform__signal_event();
    self->notify_data_available(self->connection);
    return Success(QueueCode, QUEUE_OK);
}

Result(QueueMessage) Queue__pop(QueuePtr self)
{
    QueueCode status = Queue__status(self);
    if (status != QUEUE_OK)
    {
        return Error(QueueMessage, queue_unavailable, status);
    }
    QueueCursor produced = __atomic_load_n(&self->incoming->produced, __ATOMIC_ACQUIRE);
    QueueMessageCount pending = produced - self->read;
    if (pending > QUEUE_CAPACITY)
    {
        return Error(QueueMessage, queue_corrupt_cursor, QUEUE_INVALID);
    }
    if (pending == 0)
    {
        return Error(QueueMessage, queue_empty, QUEUE_EMPTY);
    }
    QueueMessage message = self->incoming->messages[self->read % QUEUE_CAPACITY];
    // Identity comes from the protected connection, never the message's PID field.
    message.sender_pid = self->peer_pid;
    self->read++;
    __atomic_store_n(&self->outgoing->consumed, self->read, __ATOMIC_RELEASE);
    IpcEvent__ring(IpcDoorbell__mutable_event(self->peer_doorbell));
    IpcPlatform__signal_event();
    return Success(QueueMessage, message);
}

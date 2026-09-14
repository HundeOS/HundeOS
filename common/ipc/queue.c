#include "queue.h"
#include "ipc_platform.h"

static BString queue_unavailable = bstr("Queue unavailable");
static BString queue_invalid_identity = bstr("Invalid queue identity");
static BString queue_corrupt_cursor = bstr("Corrupt queue cursor");
static BString queue_full = bstr("Queue full");
static BString queue_empty = bstr("Queue empty");

Queue Queue__new(QueueIdentityPtr identity, QueuePagePtr outgoing, QueuePagePtr incoming, QueueConnectionID connection,
                 QueueParticipantID peer_pid)
{
    return (Queue){
        .identity = identity,
        .outgoing = outgoing,
        .incoming = incoming,
        .connection = connection,
        .peer_pid = peer_pid,
        .read = 0,
        .written = 0,
        .acknowledged = 0,
    };
}

Result(Queue) Queue__attach(QueueIdentityPtr identity, QueuePagePtr client, QueuePagePtr server,
                            QueueConnectionID connection, QueueParticipantID pid)
{
    if (identity->connection != connection || (pid != identity->client_pid && pid != identity->server_pid))
    {
        return Error(Queue, queue_invalid_identity, QUEUE_INVALID);
    }
    bool is_client = pid == identity->client_pid;
    Queue queue = Queue__new(identity, is_client ? client : server, is_client ? server : client, connection,
                             is_client ? identity->server_pid : identity->client_pid);
    return Success(Queue, queue);
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

QueueParticipantID Queue__peer_pid(QueuePtr self)
{
    return self->peer_pid;
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
    self->written++;
    __atomic_store_n(&self->outgoing->produced, self->written, __ATOMIC_RELEASE);
    IpcPlatform__signal_event();
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
    IpcPlatform__signal_event();
    return Success(QueueMessage, message);
}

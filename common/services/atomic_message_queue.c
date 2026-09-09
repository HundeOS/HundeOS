#include "atomic_message_queue.h"
#include "service_platform.h"

static BString queue_unavailable = bstr("Queue unavailable");
static BString queue_invalid_identity = bstr("Invalid queue identity");
static BString queue_corrupt_cursor = bstr("Corrupt queue cursor");
static BString queue_full = bstr("Queue full");
static BString queue_empty = bstr("Queue empty");

AtomicMessageQueue AtomicMessageQueue__new(AtomicMessageQueueIdentityPtr identity, AtomicMessageQueuePagePtr outgoing,
                                           AtomicMessageQueuePagePtr incoming,
                                           AtomicMessageQueueConnectionID connection,
                                           AtomicMessageQueueParticipantID peer_pid)
{
    return (AtomicMessageQueue){
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

Result(AtomicMessageQueue)
    AtomicMessageQueue__attach(AtomicMessageQueueIdentityPtr identity, AtomicMessageQueuePagePtr client,
                               AtomicMessageQueuePagePtr server, AtomicMessageQueueConnectionID connection,
                               AtomicMessageQueueParticipantID pid)
{
    if (identity->connection != connection || (pid != identity->client_pid && pid != identity->server_pid))
    {
        return Error(AtomicMessageQueue, queue_invalid_identity, ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    bool is_client = pid == identity->client_pid;
    AtomicMessageQueue queue =
        AtomicMessageQueue__new(identity, is_client ? client : server, is_client ? server : client, connection,
                                is_client ? identity->server_pid : identity->client_pid);
    return Success(AtomicMessageQueue, queue);
}

AtomicMessageQueueCode AtomicMessageQueue__status(AtomicMessageQueuePtr self)
{
    if (__atomic_load_n(&self->identity->closed, __ATOMIC_ACQUIRE) != 0)
    {
        return ATOMIC_MESSAGE_QUEUE_CLOSED;
    }
    if (__atomic_load_n(&self->identity->accepted, __ATOMIC_ACQUIRE) == 0)
    {
        return ATOMIC_MESSAGE_QUEUE_PENDING;
    }
    return ATOMIC_MESSAGE_QUEUE_OK;
}

Result(AtomicMessageQueueCode) AtomicMessageQueue__push(AtomicMessageQueuePtr self, ConstServiceQueueMessagePtr message)
{
    AtomicMessageQueueCode status = AtomicMessageQueue__status(self);
    if (status != ATOMIC_MESSAGE_QUEUE_OK)
    {
        return Error(AtomicMessageQueueCode, queue_unavailable, status);
    }
    AtomicMessageQueueCursor consumed = __atomic_load_n(&self->incoming->consumed, __ATOMIC_ACQUIRE);
    if (consumed - self->acknowledged > self->written - self->acknowledged)
    {
        return Error(AtomicMessageQueueCode, queue_corrupt_cursor, ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    self->acknowledged = consumed;
    AtomicMessageQueueMessageCount pending = self->written - consumed;
    if (pending > ATOMIC_MESSAGE_QUEUE_CAPACITY)
    {
        return Error(AtomicMessageQueueCode, queue_corrupt_cursor, ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    if (pending == ATOMIC_MESSAGE_QUEUE_CAPACITY)
    {
        return Error(AtomicMessageQueueCode, queue_full, ATOMIC_MESSAGE_QUEUE_FULL);
    }
    self->outgoing->messages[self->written % ATOMIC_MESSAGE_QUEUE_CAPACITY] = *message;
    self->written++;
    __atomic_store_n(&self->outgoing->produced, self->written, __ATOMIC_RELEASE);
    ServicePlatform__signal_event();
    return Success(AtomicMessageQueueCode, ATOMIC_MESSAGE_QUEUE_OK);
}

Result(ServiceQueueMessage) AtomicMessageQueue__pop(AtomicMessageQueuePtr self)
{
    AtomicMessageQueueCode status = AtomicMessageQueue__status(self);
    if (status != ATOMIC_MESSAGE_QUEUE_OK)
    {
        return Error(ServiceQueueMessage, queue_unavailable, status);
    }
    AtomicMessageQueueCursor produced = __atomic_load_n(&self->incoming->produced, __ATOMIC_ACQUIRE);
    AtomicMessageQueueMessageCount pending = produced - self->read;
    if (pending > ATOMIC_MESSAGE_QUEUE_CAPACITY)
    {
        return Error(ServiceQueueMessage, queue_corrupt_cursor, ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    if (pending == 0)
    {
        return Error(ServiceQueueMessage, queue_empty, ATOMIC_MESSAGE_QUEUE_EMPTY);
    }
    ServiceQueueMessage message = self->incoming->messages[self->read % ATOMIC_MESSAGE_QUEUE_CAPACITY];
    // Identity comes from the protected connection, never the message's PID field.
    message.sender_pid = self->peer_pid;
    self->read++;
    __atomic_store_n(&self->outgoing->consumed, self->read, __ATOMIC_RELEASE);
    ServicePlatform__signal_event();
    return Success(ServiceQueueMessage, message);
}

#include "atomic_message_queue.h"
#include "process.h"
#include "syscall.h"
#include "syscalls/service.h"

Result(AtomicMessageQueueArrivalsPtr) atomic_message_queue_listen(ConstBStringPtr name)
{
    SystemCallResult value = syscall(SERVICE_CALL, SERVICE_ATOMIC_MESSAGE_QUEUE_LISTEN, (Anything)BString__cstr(name),
                                     BString__length(name), 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(AtomicMessageQueueArrivalsPtr, bstr("Queue listen failed"), value);
    }
    return Success(AtomicMessageQueueArrivalsPtr, (AtomicMessageQueueArrivalsPtr)value);
}

static Result(AtomicMessageQueue) queue_attach(AtomicMessageQueueConnectionID connection)
{
    Address address = AtomicMessageQueueLayout__address(connection);
    return AtomicMessageQueue__attach(
        (AtomicMessageQueueIdentityPtr)address, (AtomicMessageQueuePagePtr)(address + HUND_SERVICE_PAGE_SIZE),
        (AtomicMessageQueuePagePtr)(address + 2 * HUND_SERVICE_PAGE_SIZE), connection, process_current_pid());
}

Result(AtomicMessageQueue) atomic_message_queue_connect(AtomicMessageQueueEndpointID endpoint)
{
    SystemCallResult value = syscall(SERVICE_CALL, SERVICE_ATOMIC_MESSAGE_QUEUE_CONNECT, endpoint, 0, 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(AtomicMessageQueue, bstr("Queue connect failed"), value);
    }
    return queue_attach(value);
}

Result(AtomicMessageQueue) atomic_message_queue_accept(AtomicMessageQueueConnectionID connection)
{
    SystemCallResult value = syscall(SERVICE_CALL, SERVICE_ATOMIC_MESSAGE_QUEUE_ACCEPT, connection, 0, 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(AtomicMessageQueue, bstr("Queue accept failed"), value);
    }
    return queue_attach(connection);
}

AtomicMessageQueueCode atomic_message_queue_close(AtomicMessageQueueConnectionID connection)
{
    return (AtomicMessageQueueCode)syscall(SERVICE_CALL, SERVICE_ATOMIC_MESSAGE_QUEUE_CLOSE, connection, 0, 0, 0);
}

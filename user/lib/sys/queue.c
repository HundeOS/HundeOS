#include "queue.h"
#include "process.h"
#include "syscall.h"
#include "syscalls/service.h"

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name)
{
    SystemCallResult value =
        syscall(SERVICE_CALL, SERVICE_QUEUE_LISTEN, (Anything)BString__cstr(name), BString__length(name), 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(QueueArrivalsPtr, bstr("Queue listen failed"), value);
    }
    return Success(QueueArrivalsPtr, (QueueArrivalsPtr)value);
}

static Result(Queue) queue_attach(QueueConnectionID connection)
{
    Address address = QueueLayout__address(connection);
    return Queue__attach((QueueIdentityPtr)address, (QueuePagePtr)(address + QUEUE_PAGE_SIZE),
                         (QueuePagePtr)(address + 2 * QUEUE_PAGE_SIZE), connection, process_current_pid());
}

Result(Queue) Queue__connect(QueueEndpointID endpoint)
{
    SystemCallResult value = syscall(SERVICE_CALL, SERVICE_QUEUE_CONNECT, endpoint, 0, 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(Queue, bstr("Queue connect failed"), value);
    }
    return queue_attach(value);
}

Result(Queue) Queue__accept(QueueConnectionID connection)
{
    SystemCallResult value = syscall(SERVICE_CALL, SERVICE_QUEUE_ACCEPT, connection, 0, 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(Queue, bstr("Queue accept failed"), value);
    }
    return queue_attach(connection);
}

QueueCode QueueConnection__close(QueueConnectionID connection)
{
    return (QueueCode)syscall(SERVICE_CALL, SERVICE_QUEUE_CLOSE, connection, 0, 0, 0);
}

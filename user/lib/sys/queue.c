#include "queue.h"
#include "queue_wait.h"
#include "process.h"
#include "syscall.h"
#include "syscalls/service.h"

static void Queue__notify_data_available(QueueConnectionID connection)
{
    syscall(SERVICE_CALL, SERVICE_QUEUE_DATA_AVAILABLE, connection, 0, 0, 0);
}

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name)
{
    SystemCallResult value =
        syscall(SERVICE_CALL, SERVICE_QUEUE_LISTEN, (Anything)BString__cstr(name), BString__length(name), 0, 0);
    if ((int64_t)value < 0)
    {
        return Error(QueueArrivalsPtr, bstr("Queue listen failed"), value);
    }
    QueueArrivalsPtr arrivals = (QueueArrivalsPtr)value;
    return Success(QueueArrivalsPtr, arrivals);
}

static Result(Queue) queue_attach(QueueConnectionID connection)
{
    Address address = QueueLayout__address(connection);
    match_result(
        Queue__attach((QueueIdentityPtr)address, (QueuePagePtr)(address + QUEUE_PAGE_SIZE),
                      (QueuePagePtr)(address + 2 * QUEUE_PAGE_SIZE), connection, process_current_pid(),
                      (IpcDoorbellPtr)QueueLayout__peer_doorbell_address(connection), &Queue__notify_data_available),
        success(queue)
        {
            return Success(Queue, queue);
        },
        error(code, message)
        {
            return Error(Queue, message, code);
        }
    );
    return Error(Queue, bstr("Queue attach failed"), QUEUE_INVALID);
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

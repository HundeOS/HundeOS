#include "sys/queue.h"
#include "sys/service.h"
#include "sys/io.h"
#include "sys/print.h"
#include "types/fixed_array.h"
#include "services/service_directory.h"
#include "ipc/ipc_platform.h"

OptionalDef(Queue);

static bool QueueTest__equals(void * first, void * second)
{
    QueuePtr a = first;
    QueuePtr b = second;
    return a->connection == b->connection;
}

FixedArrayDefine(Queue, QUEUE_LIMIT, TestQueues, &QueueTest__equals);
FixedArrayBodyDefine(TestQueues);
typedef TestQueues * TestQueuesPtr;

static bool QueueTest__contains(TestQueuesPtr queues, QueueConnectionID id)
{
    for_each_fa_ptr(
        TestQueues,
        queues,
        index,
        queue,
        {
            if (queue.connection == id)
            {
                return true;
            }
        }
    );

    return false;
}

static bool QueueTest__serve_once(TestQueuesPtr queues, QueueArrivalsPtr arrivals)
{
    bool work = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &arrivals->connections,
        index,
        id,
        {
            if (id == 0 || QueueTest__contains(queues, id))
            {
                continue;
            }
            match_result(
                Queue__accept(id),
                success(queue)
                {
                    TestQueues__push(queues, queue);
                },
                error(code, message)
                {
                    QueueConnection__close(id);
                }
            );
            work = true;
        }
    );
    for_each_fa_ptr(
        TestQueues,
        queues,
        index,
        value,
        {
            match_optional(
                TestQueues__ptr_at(queues, index),
                some(queue)
                {
                    QueueMessageCount pending =
                        queue->written - __atomic_load_n(&queue->incoming->consumed, __ATOMIC_ACQUIRE);
                    if (Queue__status(queue) == QUEUE_CLOSED || pending > QUEUE_CAPACITY)
                    {
                        QueueConnection__close(queue->connection);
                        TestQueues__remove(queues, value);
                        work = true;
                        break;
                    }
                    if (pending == QUEUE_CAPACITY)
                    {
                        continue;
                    }
                    match_result(
                        Queue__pop(queue),
                        success(message)
                        {
                            match_result(
                                Queue__push(queue, &message),
                                success_do_nothing(),
                                error(code, message)
                                {
                                    QueueConnection__close(queue->connection);
                                    TestQueues__remove(queues, value);
                                    return true;
                                }
                            );
                            work = true;
                        },
                        error(code, message)
                        {
                            if (code != QUEUE_EMPTY)
                            {
                                QueueConnection__close(queue->connection);
                                TestQueues__remove(queues, value);
                                return true;
                            }
                        }
                    );
                },
                none_do_nothing()
            );
        }
    );
    return work;
}

static int QueueTest__server(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    match_result(
        Queue__listen(&bstr("queue-test")),
        success(arrivals)
        {
            TestQueues queues = TestQueues__new();
            IO__print(&bstr("queue_test: listening\n"));
            while (true)
            {
                ConstIpcEventPtr event = IpcDoorbell__event(IpcPlatform__local_doorbell());
                IpcEventGeneration observed = IpcPlatform__prepare_wait(event);
                if (!QueueTest__serve_once(&queues, arrivals))
                {
                    IpcPlatform__wait_event(event, observed);
                }
            }
        },
        error(code, message)
        {
            IO__printf(&bstr("queue_test: listen error %i\n"), (int)code);
        }
    );
    return 1;
}

static int QueueTest__exchange(QueuePtr queue)
{
    for (uint64_t sequence = 0; sequence < 128; sequence++)
    {
        Queue__wait_writable(queue);
        QueueMessage request = QueueMessage__new(9999, 1);
        request.arg0 = sequence;
        match_result(
            Queue__push(queue, &request),
            success_do_nothing(),
            error(code, message)
            {
                IO__printf(&bstr("queue_test: send error %i at %i\n"), (int)code, (int)sequence);
                return 1;
            }
        );
        Queue__wait_readable(queue);
        match_result(
            Queue__pop(queue),
            success(reply)
            {
                if (reply.arg0 != sequence || reply.sender_pid != queue->peer_pid)
                {
                    IO__printf(&bstr("queue_test: reply mismatch at %i: sequence=%i sender=%i peer=%i\n"),
                               (int)sequence, (int)reply.arg0, (int)reply.sender_pid, (int)queue->peer_pid);
                    return 1;
                }
            },
            error(code, message)
            {
                IO__printf(&bstr("queue_test: receive error %i\n"), (int)code);
                return 1;
            }
        );
    }
    return 0;
}

int main(int argc, char ** argv)
{
    bool probe_identity = false;
    bool probe_peer = false;
    if (argc == 2)
    {
        BString mode = BString__new(argv[1]);
        if (BString__equals(&mode, &bstr("server")))
        {
            return QueueTest__server();
        }
        probe_identity = BString__equals(&mode, &bstr("probe-identity"));
        probe_peer = BString__equals(&mode, &bstr("probe-peer"));
        if (!probe_identity && !probe_peer)
        {
            return 1;
        }
    }

    QueueEndpointID endpoint;
    match_optional(
        ServiceDirectory__find_endpoint(ServiceDirectory__shared(), &bstr("queue-test")),
        some(value)
        {
            endpoint = value;
        },
        none()
        {
            IO__print(&bstr("queue_test: server unavailable\n"));
            return 1;
        }
    );
    match_result(
        Queue__connect(endpoint),
        success(queue)
        {
            if (probe_identity || probe_peer)
            {
                IO__print(&bstr("queue_test: protection probe expects a process fault\n"));
                if (probe_identity)
                {
                    __atomic_store_n(&queue.identity->client_pid, 0, __ATOMIC_RELEASE);
                }
                if (probe_peer)
                {
                    __atomic_store_n(&queue.incoming->produced, 0, __ATOMIC_RELEASE);
                }
                IO__print(&bstr("queue_test: ERROR protected page was writable\n"));
                QueueConnection__close(queue.connection);
                return 1;
            }
            int result = QueueTest__exchange(&queue);
            QueueCode status = QueueConnection__close(queue.connection);
            if (result != 0 || status != QUEUE_OK)
            {
                IO__printf(&bstr("queue_test: exchange failed result=%i close=%i\n"), result, (int)status);
                return 1;
            }
            IO__print(&bstr("queue_test: 128 round trips passed\n"));
            return 0;
        },
        error(code, message)
        {
            IO__printf(&bstr("queue_test: connect error %i\n"), (int)code);
        }
    );
    return 1;
}

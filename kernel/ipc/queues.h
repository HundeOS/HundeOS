#ifndef KERNEL_QUEUES_H
#define KERNEL_QUEUES_H

#include "../../common/ipc/queue.h"
#include "../../common/ipc/ipc_platform.h"
#include "../process.h"
#include "../service/service_directory.h"
#include "../types/fixed_array.h"

typedef struct QueueListener
{
    UserAddress arrivals;
    Optional(ProcessPtr) process;
    QueueEndpointID endpoint;
} QueueListener;

typedef QueueListener * QueueListenerPtr;
OptionalDef(QueueListener);
bool QueueListener__equals(void * first, void * second);
FixedArrayDefine(QueueListener, QUEUE_LISTENER_LIMIT, QueueListeners, &QueueListener__equals);

typedef struct QueueConnection
{
    UserAddress memory;
    UserAddress client_doorbell;
    UserAddress server_doorbell;
    Optional(ProcessPtr) client;
    Optional(ProcessPtr) server;
    Optional(ProcessPtr) kernel_origin;
    QueueConnectionID connection;
    QueueSlotIndex listener_index;
    bool kernel_client;
    bool client_peer_doorbell_mapped;
    bool server_peer_doorbell_mapped;
    uint64_t scheduled_generation;
    bool scheduled_reply;
} QueueConnection;

typedef QueueConnection * QueueConnectionPtr;
OptionalDef(QueueConnection);
bool QueueConnection__equals(void * first, void * second);
FixedArrayDefine(QueueConnection, QUEUE_LIMIT, QueueConnections, &QueueConnection__equals);

typedef struct Queues
{
    QueueListeners listeners;
    QueueConnections connections;
    QueueHandleGeneration generation;
    Optional(UserAddress) kernel_doorbell;
} Queues;

typedef Queues * QueuesPtr;

#define QUEUES_INIT                                                                                                    \
    (Queues)                                                                                                           \
    {                                                                                                                  \
        .listeners = EmptyArray(QueueListeners), .connections = EmptyArray(QueueConnections), .generation = 0,         \
        .kernel_doorbell = None(UserAddress),                                                                          \
    }

// Callers hold the scheduler lock to serialize mappings with process exit.
Result(Address) Queues__listen(QueuesPtr self, ProcessPtr process, ConstBStringPtr name);
Result(QueueConnectionID) Queues__connect(QueuesPtr self, ProcessPtr process, QueueEndpointID endpoint);
Result(QueueCode) Queues__accept(QueuesPtr self, ProcessPtr process, QueueConnectionID connection);
Result(QueueCode) Queues__close(QueuesPtr self, ProcessPtr process, QueueConnectionID connection);
Result(ProcessPtr) Queues__client_process(QueuesPtr self, ProcessPtr server, QueueConnectionID connection);
Result(ProcessPtr) Queues__peer_process(QueuesPtr self, ProcessPtr process, QueueConnectionID connection);
void Queues__exit(QueuesPtr self, ProcessPtr process);

Result(Queue) Queues__connect_kernel(QueuesPtr self, QueueEndpointID endpoint, OptionalPtr(ProcessPtr) origin);
Result(QueueCode) Queues__close_kernel(QueuesPtr self, QueueConnectionID connection);
bool Queues__has_kernel_client(QueuesPtr self, ProcessPtr server);
bool Queues__has_unread_messages(QueuesPtr self, ProcessPtr process);
bool Queues__has_waitable_state(QueuesPtr self, ProcessPtr process);

#endif

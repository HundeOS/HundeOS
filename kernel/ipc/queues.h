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
    Optional(ProcessPtr) client;
    Optional(ProcessPtr) server;
    QueueConnectionID connection;
    QueueSlotIndex listener_index;
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
} Queues;
typedef Queues * QueuesPtr;

#define QUEUES_INIT                                                                                                    \
    (Queues){.listeners = EmptyArray(QueueListeners), .connections = EmptyArray(QueueConnections), .generation = 0}

// Callers hold the scheduler lock to serialize mappings with process exit.
Result(Address) Queues__listen(QueuesPtr self, ProcessPtr process, ConstBStringPtr name);
Result(QueueConnectionID) Queues__connect(QueuesPtr self, ProcessPtr process, QueueEndpointID endpoint);
Result(QueueCode) Queues__accept(QueuesPtr self, ProcessPtr process, QueueConnectionID connection);
Result(QueueCode) Queues__close(QueuesPtr self, ProcessPtr process, QueueConnectionID connection);
Result(ProcessPtr) Queues__client_process(QueuesPtr self, ProcessPtr server, QueueConnectionID connection);
void Queues__exit(QueuesPtr self, ProcessPtr process);

#endif

#ifndef KERNEL_ATOMIC_MESSAGE_QUEUE_H
#define KERNEL_ATOMIC_MESSAGE_QUEUE_H

#include "../../common/services/atomic_message_queue.h"
#include "../../common/services/service_platform.h"
#include "../process.h"
#include "../types/fixed_array.h"
#include "service_directory.h"

typedef struct AtomicMessageQueueListener
{
    UserAddress arrivals;
    Optional(ProcessPtr) process;
    AtomicMessageQueueEndpointID endpoint;
} AtomicMessageQueueListener;
typedef AtomicMessageQueueListener * AtomicMessageQueueListenerPtr;
OptionalDef(AtomicMessageQueueListener);
bool AtomicMessageQueueListener__equals(void * first, void * second);
FixedArrayDefine(AtomicMessageQueueListener, ATOMIC_MESSAGE_QUEUE_LIMIT, AtomicMessageQueueListeners,
                 &AtomicMessageQueueListener__equals);

typedef struct AtomicMessageQueueConnection
{
    UserAddress memory;
    Optional(ProcessPtr) client;
    Optional(ProcessPtr) server;
    AtomicMessageQueueConnectionID connection;
    AtomicMessageQueueSlotIndex listener_index;
} AtomicMessageQueueConnection;
typedef AtomicMessageQueueConnection * AtomicMessageQueueConnectionPtr;
OptionalDef(AtomicMessageQueueConnection);
bool AtomicMessageQueueConnection__equals(void * first, void * second);
FixedArrayDefine(AtomicMessageQueueConnection, ATOMIC_MESSAGE_QUEUE_LIMIT, AtomicMessageQueueConnections,
                 &AtomicMessageQueueConnection__equals);

typedef struct AtomicMessageQueues
{
    AtomicMessageQueueListeners listeners;
    AtomicMessageQueueConnections connections;
    AtomicMessageQueueHandleGeneration generation;
} AtomicMessageQueues;
typedef AtomicMessageQueues * AtomicMessageQueuesPtr;

#define ATOMIC_MESSAGE_QUEUES_INIT                                                                                     \
    (AtomicMessageQueues){.listeners = EmptyArray(AtomicMessageQueueListeners),                                        \
                          .connections = EmptyArray(AtomicMessageQueueConnections),                                    \
                          .generation = 0}

// Callers hold the scheduler lock to serialize mappings with process exit.
Result(Address) AtomicMessageQueues__listen(AtomicMessageQueuesPtr self, ProcessPtr process, ConstBStringPtr name);
Result(AtomicMessageQueueConnectionID) AtomicMessageQueues__connect(AtomicMessageQueuesPtr self, ProcessPtr process,
                                                                    AtomicMessageQueueEndpointID endpoint);
Result(AtomicMessageQueueCode) AtomicMessageQueues__accept(AtomicMessageQueuesPtr self, ProcessPtr process,
                                                           AtomicMessageQueueConnectionID connection);
Result(AtomicMessageQueueCode) AtomicMessageQueues__close(AtomicMessageQueuesPtr self, ProcessPtr process,
                                                          AtomicMessageQueueConnectionID connection);
void AtomicMessageQueues__exit(AtomicMessageQueuesPtr self, ProcessPtr process);

#endif

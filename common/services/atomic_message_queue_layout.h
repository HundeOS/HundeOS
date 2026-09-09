#ifndef HUND_ATOMIC_MESSAGE_QUEUE_LAYOUT_H
#define HUND_ATOMIC_MESSAGE_QUEUE_LAYOUT_H

#include "service_directory.h"
#include "../types/address.h"
#include "../types/atomic_fixed_array.h"

#define ATOMIC_MESSAGE_QUEUE_LIMIT 16U
#define ATOMIC_MESSAGE_QUEUE_LISTENER_BASE ((Address)0x72000000ULL)
#define ATOMIC_MESSAGE_QUEUE_CONNECTION_BASE ((Address)0x72100000ULL)
#define ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE (3 * HUND_SERVICE_PAGE_SIZE)
#define ATOMIC_MESSAGE_QUEUE_CAPACITY 32U

typedef enum AtomicMessageQueueCode
{
    ATOMIC_MESSAGE_QUEUE_OK = 0,
    ATOMIC_MESSAGE_QUEUE_EMPTY = -100,
    ATOMIC_MESSAGE_QUEUE_FULL = -101,
    ATOMIC_MESSAGE_QUEUE_CLOSED = -102,
    ATOMIC_MESSAGE_QUEUE_INVALID = -103,
    ATOMIC_MESSAGE_QUEUE_DENIED = -104,
    ATOMIC_MESSAGE_QUEUE_OCCUPIED = -105,
    ATOMIC_MESSAGE_QUEUE_NO_MEMORY = -106,
    ATOMIC_MESSAGE_QUEUE_PENDING = -107,
} AtomicMessageQueueCode;

// Each page has one writer. consumed acknowledges the opposite page's messages.
typedef struct AtomicMessageQueuePage
{
    AtomicMessageQueueCursor produced;
    AtomicMessageQueueCursor consumed;
    ServiceQueueMessage messages[ATOMIC_MESSAGE_QUEUE_CAPACITY];
} AtomicMessageQueuePage;
typedef AtomicMessageQueuePage * AtomicMessageQueuePagePtr;

typedef struct AtomicMessageQueueIdentity
{
    AtomicMessageQueueConnectionID connection;
    AtomicMessageQueueEndpointID endpoint;
    AtomicMessageQueueParticipantID client_pid;
    AtomicMessageQueueParticipantID server_pid;
    AtomicMessageQueueStateFlag accepted;
    AtomicMessageQueueStateFlag closed;
} AtomicMessageQueueIdentity;
typedef AtomicMessageQueueIdentity * AtomicMessageQueueIdentityPtr;

AtomicFixedArrayDefine(AtomicMessageQueueConnectionID, ATOMIC_MESSAGE_QUEUE_LIMIT, AtomicMessageQueueConnectionSlots);

typedef struct AtomicMessageQueueArrivals
{
    AtomicMessageQueueEndpointID endpoint;
    AtomicMessageQueueEpoch epoch;
    AtomicMessageQueueConnectionSlots connections;
} AtomicMessageQueueArrivals;
typedef AtomicMessageQueueArrivals * AtomicMessageQueueArrivalsPtr;

_Static_assert(sizeof(AtomicMessageQueuePage) <= HUND_SERVICE_PAGE_SIZE, "Queue page overflow");
_Static_assert(sizeof(AtomicMessageQueueIdentity) <= HUND_SERVICE_PAGE_SIZE, "Identity page overflow");
_Static_assert(sizeof(AtomicMessageQueueArrivals) <= HUND_SERVICE_PAGE_SIZE, "Arrivals page overflow");
_Static_assert(offsetof(AtomicMessageQueueArrivals, connections) == 2 * sizeof(uint64_t),
               "Arrivals slot offset changed");
_Static_assert(sizeof(AtomicMessageQueueArrivals) == (2 + ATOMIC_MESSAGE_QUEUE_LIMIT) * sizeof(uint64_t),
               "Arrivals layout changed");

Address AtomicMessageQueueLayout__address(AtomicMessageQueueConnectionID connection);
Optional(AtomicMessageQueueConnectionID)
    AtomicMessageQueueArrivals__connection(AtomicMessageQueueArrivalsPtr self, AtomicMessageQueueSlotIndex index);
AtomicMessageQueueEpoch AtomicMessageQueueArrivals__epoch(AtomicMessageQueueArrivalsPtr self);

#endif

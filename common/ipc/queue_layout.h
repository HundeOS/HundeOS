#ifndef HUND_QUEUE_LAYOUT_H
#define HUND_QUEUE_LAYOUT_H

#include "queue_types.h"
#include "kernel_notification_queue.h"
#include "../types/address.h"
#include "../types/atomic_fixed_array.h"
#include "../types/atomic_record_array.h"

#define QUEUE_LIMIT 1024U
#define QUEUE_LISTENER_LIMIT 1024U
#define QUEUE_PAGE_SIZE 4096ULL
#define QUEUE_LISTENER_BASE ((Address)0x74000000ULL)
#define QUEUE_CONNECTION_BASE ((Address)0x72400000ULL)
#define QUEUE_CONNECTION_SIZE (3 * QUEUE_PAGE_SIZE)
#define QUEUE_CAPACITY 32U

typedef enum QueueCode
{
    QUEUE_OK = 0,
    QUEUE_EMPTY = -100,
    QUEUE_FULL = -101,
    QUEUE_CLOSED = -102,
    QUEUE_INVALID = -103,
    QUEUE_DENIED = -104,
    QUEUE_OCCUPIED = -105,
    QUEUE_NO_MEMORY = -106,
    QUEUE_PENDING = -107,
} QueueCode;

typedef struct QueuePublication
{
    QueueMessage message;
    QueueCursor cursor;
} QueuePublication;

ResultDef(QueuePublication);
AtomicRecordArrayDefine(QueuePublication, 1, QueuePublications);

// Each page has one writer. consumed acknowledges the opposite page's messages.
typedef struct QueuePage
{
    QueueCursor produced;
    QueueCursor consumed;
    QueueMessage messages[QUEUE_CAPACITY];
    QueuePublications publication;
} QueuePage;

typedef QueuePage * QueuePagePtr;

typedef struct QueueIdentity
{
    QueueConnectionID connection;
    QueueEndpointID endpoint;
    QueueParticipantID client_pid;
    QueueParticipantID server_pid;
    QueueParticipantID origin_pid;
    QueueStateFlag accepted;
    QueueStateFlag closed;
} QueueIdentity;

typedef QueueIdentity * QueueIdentityPtr;

AtomicFixedArrayDefine(QueueConnectionID, QUEUE_LIMIT, QueueConnectionSlots);

typedef struct QueueArrivals
{
    QueueEndpointID endpoint;
    QueueEpoch epoch;
    QueueConnectionSlots connections;
} QueueArrivals;

typedef QueueArrivals * QueueArrivalsPtr;

#define QUEUE_ARRIVALS_SIZE ((sizeof(QueueArrivals) + QUEUE_PAGE_SIZE - 1) / QUEUE_PAGE_SIZE * QUEUE_PAGE_SIZE)

_Static_assert(sizeof(QueuePage) <= QUEUE_PAGE_SIZE, "Queue page overflow");
_Static_assert(sizeof(QueueIdentity) <= QUEUE_PAGE_SIZE, "Identity page overflow");
_Static_assert(sizeof(QueueArrivals) <= QUEUE_ARRIVALS_SIZE, "Arrivals region overflow");
_Static_assert(QUEUE_LISTENER_BASE + QUEUE_LISTENER_LIMIT * QUEUE_ARRIVALS_SIZE <= 0x75000000ULL,
               "Arrivals region overflow");
_Static_assert(KERNEL_NOTIFICATION_QUEUE_ADDRESS + KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE <= QUEUE_CONNECTION_BASE,
               "Kernel notification queue overlaps connections");
_Static_assert(QUEUE_CONNECTION_BASE + QUEUE_LIMIT * QUEUE_CONNECTION_SIZE <= 0x73000000ULL,
               "Connection region overflow");
_Static_assert(QUEUE_CONNECTION_BASE + QUEUE_LIMIT * QUEUE_CONNECTION_SIZE <= QUEUE_LISTENER_BASE,
               "Connections overlap arrivals");
_Static_assert(offsetof(QueueArrivals, connections) == 2 * sizeof(uint64_t), "Arrivals slot offset changed");
_Static_assert(sizeof(QueueArrivals) == (2 + QUEUE_LIMIT) * sizeof(uint64_t), "Arrivals layout changed");

Address QueueLayout__address(QueueConnectionID connection);
Optional(QueueConnectionID) QueueArrivals__connection(QueueArrivalsPtr self, QueueSlotIndex index);
QueueEpoch QueueArrivals__epoch(QueueArrivalsPtr self);

#endif

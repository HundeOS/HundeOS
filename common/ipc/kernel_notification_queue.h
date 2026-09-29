#ifndef HUND_KERNEL_NOTIFICATION_QUEUE_H
#define HUND_KERNEL_NOTIFICATION_QUEUE_H

#include "../types/address.h"
#include "../types/atomic_fixed_array.h"

#define KERNEL_NOTIFICATION_QUEUE_ADDRESS ((Address)0x72300000ULL)
#define KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE 4096ULL
#define KERNEL_NOTIFICATION_QUEUE_CAPACITY 256U

typedef uint64_t KernelNotificationID;
typedef uint64_t KernelNotificationSequence;

typedef struct KernelNotification
{
    KernelNotificationID id;
    KernelNotificationSequence sequence;
    uint64_t occurred_at_usec;
} KernelNotification;

typedef KernelNotification * KernelNotificationPtr;
typedef const KernelNotification * ConstKernelNotificationPtr;
OptionalDef(KernelNotification);

static inline KernelNotification KernelNotification__new(KernelNotificationID id, KernelNotificationSequence sequence,
                                                         uint64_t occurred_at_usec)
{
    return (KernelNotification){.id = id, .sequence = sequence, .occurred_at_usec = occurred_at_usec};
}

AtomicFixedArrayDefine(KernelNotificationSequence, KERNEL_NOTIFICATION_QUEUE_CAPACITY, KernelNotificationSlots);
AtomicFixedArrayDefine(uint64_t, KERNEL_NOTIFICATION_QUEUE_CAPACITY, KernelNotificationTimestamps);

typedef struct KernelNotificationQueue
{
    KernelNotificationSlots pending;
    KernelNotificationTimestamps occurred_at_usec;
} KernelNotificationQueue;

typedef KernelNotificationQueue * KernelNotificationQueuePtr;

_Static_assert(sizeof(KernelNotificationQueue) <= KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE,
               "Kernel notification queue exceeds its shared page");

KernelNotificationQueue KernelNotificationQueue__new(void);
Result(CollectionCode) KernelNotificationQueue__publish(KernelNotificationQueuePtr self, KernelNotificationID id,
                                                        KernelNotificationSequence sequence, uint64_t occurred_at_usec);
Optional(KernelNotification) KernelNotificationQueue__pop(KernelNotificationQueuePtr self);
bool KernelNotificationQueue__has_pending(KernelNotificationQueuePtr self);

#endif

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
} KernelNotification;
typedef KernelNotification * KernelNotificationPtr;
OptionalDef(KernelNotification);

static inline KernelNotification KernelNotification__new(KernelNotificationID id, KernelNotificationSequence sequence)
{
    return (KernelNotification){.id = id, .sequence = sequence};
}

AtomicFixedArrayDefine(KernelNotificationSequence, KERNEL_NOTIFICATION_QUEUE_CAPACITY, KernelNotificationSlots);

typedef struct KernelNotificationQueue
{
    KernelNotificationSlots pending;
} KernelNotificationQueue;
typedef KernelNotificationQueue * KernelNotificationQueuePtr;

KernelNotificationQueue KernelNotificationQueue__new(void);
Result(CollectionCode) KernelNotificationQueue__publish(KernelNotificationQueuePtr self, KernelNotificationID id,
                                                        KernelNotificationSequence sequence);
Optional(KernelNotification) KernelNotificationQueue__pop(KernelNotificationQueuePtr self);

#endif

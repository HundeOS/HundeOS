#ifndef HUND_INTERRUPT_CLIENT_H
#define HUND_INTERRUPT_CLIENT_H
#include "ipc/interrupt_protocol.h"
#include "sys/service.h"
#include "channel/channel.h"

typedef struct InterruptDeliveryTiming
{
    InterruptDeliverySequence sequence;
    uint64_t kernel_irq_at_usec;
    uint64_t manager_received_at_usec;
    uint64_t driver_received_at_usec;
} InterruptDeliveryTiming;

typedef InterruptDeliveryTiming * InterruptDeliveryTimingPtr;
typedef const InterruptDeliveryTiming * ConstInterruptDeliveryTimingPtr;
OptionalDef(InterruptDeliveryTiming);

InterruptDeliveryTiming InterruptDeliveryTiming__new(InterruptDeliverySequence sequence, uint64_t kernel_irq_at_usec,
                                                     uint64_t manager_received_at_usec,
                                                     uint64_t driver_received_at_usec);
ServiceCode InterruptClient__subscribe(IRQNumber irq);
ServiceCode InterruptClient__poll(ServiceEventPtr event);
ServiceCode InterruptClient__wait(ServiceEventPtr event);
ServiceCode InterruptClient__acknowledge(IRQNumber irq);
Optional(InterruptDeliveryTiming) InterruptClient__delivery_timing(InterruptDeliverySequence sequence);
#endif

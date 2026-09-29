#ifndef HUND_INTERRUPT_PROTOCOL_H
#define HUND_INTERRUPT_PROTOCOL_H

#include "../interrupts/interrupt_types.h"

#define INTERRUPT_MANAGER_SERVICE_NAME "hund-unterbrict"

typedef uint64_t InterruptRegistrationGeneration;

typedef enum InterruptProtocolRequestKind
{
    INTERRUPT_PROTOCOL_SUBSCRIBE = 1,
    INTERRUPT_PROTOCOL_WAIT = 2,
    INTERRUPT_PROTOCOL_ACKNOWLEDGE = 3,
} InterruptProtocolRequestKind;

typedef enum InterruptProtocolResponseKind
{
    INTERRUPT_PROTOCOL_SUBSCRIBED = 1,
    INTERRUPT_PROTOCOL_DELIVERY = 2,
    INTERRUPT_PROTOCOL_ACKNOWLEDGED = 3,
    INTERRUPT_PROTOCOL_ERROR = 4,
} InterruptProtocolResponseKind;

typedef enum InterruptProtocolCode
{
    INTERRUPT_PROTOCOL_OK = 0,
    INTERRUPT_PROTOCOL_INVALID = -1,
    INTERRUPT_PROTOCOL_DENIED = -2,
    INTERRUPT_PROTOCOL_ALREADY_REGISTERED = -3,
    INTERRUPT_PROTOCOL_STALE_ACKNOWLEDGEMENT = -4,
} InterruptProtocolCode;

typedef struct InterruptProtocolRequest
{
    InterruptProtocolRequestKind kind;
    IRQNumber irq;
    InterruptRegistrationGeneration generation;
    InterruptDeliverySequence sequence;
} InterruptProtocolRequest;

typedef InterruptProtocolRequest * InterruptProtocolRequestPtr;
typedef const InterruptProtocolRequest * ConstInterruptProtocolRequestPtr;

typedef struct InterruptProtocolResponse
{
    InterruptProtocolResponseKind kind;
    InterruptProtocolCode code;
    IRQNumber irq;
    InterruptRegistrationGeneration generation;
    InterruptDeliverySequence sequence;
    uint64_t kernel_irq_at_usec;
    uint64_t manager_received_at_usec;
} InterruptProtocolResponse;

typedef InterruptProtocolResponse * InterruptProtocolResponsePtr;
typedef const InterruptProtocolResponse * ConstInterruptProtocolResponsePtr;

InterruptProtocolRequest InterruptProtocol__subscribe(IRQNumber irq);
InterruptProtocolRequest InterruptProtocol__wait(IRQNumber irq, InterruptRegistrationGeneration generation);
InterruptProtocolRequest InterruptProtocol__acknowledgement(IRQNumber irq, InterruptRegistrationGeneration generation,
                                                            InterruptDeliverySequence sequence);
InterruptProtocolResponse InterruptProtocol__subscribed(IRQNumber irq, InterruptRegistrationGeneration generation);
InterruptProtocolResponse InterruptProtocol__delivery(IRQNumber irq, InterruptRegistrationGeneration generation,
                                                      InterruptDeliverySequence sequence, uint64_t kernel_irq_at_usec,
                                                      uint64_t manager_received_at_usec);
InterruptProtocolResponse InterruptProtocol__acknowledged(IRQNumber irq, InterruptRegistrationGeneration generation,
                                                          InterruptDeliverySequence sequence);
InterruptProtocolResponse InterruptProtocol__error(IRQNumber irq, InterruptProtocolCode code);

#endif

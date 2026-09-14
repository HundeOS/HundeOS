#include "interrupt_protocol.h"

static InterruptProtocolRequest InterruptProtocolRequest__new(InterruptProtocolRequestKind kind, IRQNumber irq,
                                                              InterruptRegistrationGeneration generation,
                                                              InterruptDeliverySequence sequence)
{
    return (InterruptProtocolRequest){.kind = kind, .irq = irq, .generation = generation, .sequence = sequence};
}

static InterruptProtocolResponse InterruptProtocolResponse__new(InterruptProtocolResponseKind kind,
                                                                InterruptProtocolCode code, IRQNumber irq,
                                                                InterruptRegistrationGeneration generation,
                                                                InterruptDeliverySequence sequence)
{
    return (InterruptProtocolResponse){
        .kind = kind, .code = code, .irq = irq, .generation = generation, .sequence = sequence};
}

InterruptProtocolRequest InterruptProtocol__subscribe(IRQNumber irq)
{
    return InterruptProtocolRequest__new(INTERRUPT_PROTOCOL_SUBSCRIBE, irq, 0, 0);
}

InterruptProtocolRequest InterruptProtocol__wait(IRQNumber irq, InterruptRegistrationGeneration generation)
{
    return InterruptProtocolRequest__new(INTERRUPT_PROTOCOL_WAIT, irq, generation, 0);
}

InterruptProtocolRequest InterruptProtocol__acknowledgement(IRQNumber irq, InterruptRegistrationGeneration generation,
                                                            InterruptDeliverySequence sequence)
{
    return InterruptProtocolRequest__new(INTERRUPT_PROTOCOL_ACKNOWLEDGE, irq, generation, sequence);
}

InterruptProtocolResponse InterruptProtocol__subscribed(IRQNumber irq, InterruptRegistrationGeneration generation)
{
    return InterruptProtocolResponse__new(INTERRUPT_PROTOCOL_SUBSCRIBED, INTERRUPT_PROTOCOL_OK, irq, generation, 0);
}

InterruptProtocolResponse InterruptProtocol__delivery(IRQNumber irq, InterruptRegistrationGeneration generation,
                                                      InterruptDeliverySequence sequence)
{
    return InterruptProtocolResponse__new(INTERRUPT_PROTOCOL_DELIVERY, INTERRUPT_PROTOCOL_OK, irq, generation,
                                          sequence);
}

InterruptProtocolResponse InterruptProtocol__acknowledged(IRQNumber irq, InterruptRegistrationGeneration generation,
                                                          InterruptDeliverySequence sequence)
{
    return InterruptProtocolResponse__new(INTERRUPT_PROTOCOL_ACKNOWLEDGED, INTERRUPT_PROTOCOL_OK, irq, generation,
                                          sequence);
}

InterruptProtocolResponse InterruptProtocol__error(IRQNumber irq, InterruptProtocolCode code)
{
    return InterruptProtocolResponse__new(INTERRUPT_PROTOCOL_ERROR, code, irq, 0, 0);
}

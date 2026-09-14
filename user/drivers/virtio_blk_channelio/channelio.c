#include "channelio.h"

#include "channel/channel.h"
#include "services/service_directory.h"

OptionalDef(ChannelPtr);

static Channel interrupt_channel_storage;
static Optional(ChannelPtr) interrupt_channel = None(ChannelPtr);
static IRQNumber subscribed_irq;
static InterruptRegistrationGeneration registration_generation;
static InterruptDeliverySequence delivery_sequence;

static ServiceCode VirtioBlkChannelIO__request_wait(ChannelPtr channel)
{
    InterruptProtocolRequest request = InterruptProtocol__wait(subscribed_irq, registration_generation);
    match_result(
        Channel__request(channel, (ConstChannelDataPtr)&request, sizeof(request)),
        success(code) { return SERVICE_OK; },
        error(code, message) { return SERVICE_IRQ_REGISTRATION_FAILED; }
    );
    return SERVICE_IRQ_REGISTRATION_FAILED;
}

ServiceCode VirtioBlkChannelIO__subscribe(IRQNumber irq)
{
    if (!service_ok(service_wait_ready(&bstr(INTERRUPT_MANAGER_SERVICE_NAME))))
    {
        return SERVICE_IRQ_REGISTRATION_FAILED;
    }
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(INTERRUPT_MANAGER_SERVICE_NAME)),
        success(channel)
        {
            InterruptProtocolRequest request = InterruptProtocol__subscribe(irq);
            InterruptProtocolResponse response;
            match_result(
                Channel__submit(&channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&response,
                                sizeof(response)),
                success(size)
                {
                    if (size != sizeof(response) || response.kind != INTERRUPT_PROTOCOL_SUBSCRIBED ||
                        response.code != INTERRUPT_PROTOCOL_OK || response.irq != irq || response.generation == 0)
                    {
                        Channel__close(&channel);
                        return SERVICE_IRQ_REGISTRATION_FAILED;
                    }
                    subscribed_irq = irq;
                    registration_generation = response.generation;
                    interrupt_channel_storage = channel;
                    interrupt_channel = Some(ChannelPtr, &interrupt_channel_storage);
                    return VirtioBlkChannelIO__request_wait(&interrupt_channel_storage);
                },
                error(code, message)
                {
                    Channel__close(&channel);
                    return SERVICE_IRQ_REGISTRATION_FAILED;
                }
            );
        },
        error(code, message) { return SERVICE_IRQ_REGISTRATION_FAILED; }
    );
    return SERVICE_IRQ_REGISTRATION_FAILED;
}

ServiceCode VirtioBlkChannelIO__poll(ServiceEventPtr event)
{
    ServiceCode poll = service_poll(event);
    if (service_ok(poll) || poll != SERVICE_EVENT_QUEUE_EMPTY)
    {
        return poll;
    }
    match_optional(
        interrupt_channel,
        some(channel)
        {
            if (delivery_sequence != 0)
            {
                return SERVICE_EVENT_QUEUE_EMPTY;
            }
            InterruptProtocolResponse response;
            match_result(
                Channel__poll_reply(channel, (ChannelDataPtr)&response, sizeof(response)),
                success(optional_size)
                {
                    match_optional(
                        optional_size,
                        some(size)
                        {
                            if (size == sizeof(response) && response.kind == INTERRUPT_PROTOCOL_DELIVERY &&
                                response.code == INTERRUPT_PROTOCOL_OK && response.irq == subscribed_irq &&
                                response.generation == registration_generation && response.sequence != 0)
                            {
                                delivery_sequence = response.sequence;
                                *event = ServiceEvent__new(SERVICE_EVENT_IRQ, subscribed_irq, delivery_sequence);
                                return SERVICE_OK;
                            }
                            return SERVICE_ABORT;
                        },
                        none_do_nothing()
                    );
                },
                error(code, message) { return SERVICE_ABORT; }
            );
        },
        none() { return SERVICE_ABORT; }
    );
    return SERVICE_EVENT_QUEUE_EMPTY;
}

ServiceCode VirtioBlkChannelIO__wait(ServiceEventPtr event)
{
    Channel__prepare_wait();
    while (true)
    {
        ServiceCode poll = VirtioBlkChannelIO__poll(event);
        if (poll != SERVICE_EVENT_QUEUE_EMPTY)
        {
            return poll;
        }
        Channel__wait_event();
    }
}

ServiceCode VirtioBlkChannelIO__acknowledge(IRQNumber irq)
{
    match_optional(
        interrupt_channel,
        some(channel)
        {
            InterruptProtocolRequest request =
                InterruptProtocol__acknowledgement(irq, registration_generation, delivery_sequence);
            InterruptProtocolResponse response;
            match_result(
                Channel__submit(channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&response,
                                sizeof(response)),
                success(size)
                {
                    if (size == sizeof(response) && response.kind == INTERRUPT_PROTOCOL_ACKNOWLEDGED &&
                        response.code == INTERRUPT_PROTOCOL_OK && response.irq == irq &&
                        response.generation == registration_generation && response.sequence == delivery_sequence)
                    {
                        delivery_sequence = 0;
                        return VirtioBlkChannelIO__request_wait(channel);
                    }
                    return SERVICE_ABORT;
                },
                error(code, message) { return SERVICE_ABORT; }
            );
        },
        none() { return SERVICE_ABORT; }
    );
    return SERVICE_ABORT;
}

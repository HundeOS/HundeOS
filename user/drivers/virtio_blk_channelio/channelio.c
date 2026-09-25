#include "channelio.h"
#include "interrupt/client.h"

ServiceCode VirtioBlkChannelIO__subscribe(IRQNumber irq)
{
    return InterruptClient__subscribe(irq);
}

ServiceCode VirtioBlkChannelIO__acknowledge(IRQNumber irq)
{
    return InterruptClient__acknowledge(irq);
}

ServiceCode VirtioBlkChannelIO__poll(ServiceEventPtr event)
{
    ServiceCode status = service_poll(event);
    return status == SERVICE_EVENT_QUEUE_EMPTY ? InterruptClient__poll(event) : status;
}

ServiceCode VirtioBlkChannelIO__wait(ServiceEventPtr event)
{
    Channel__prepare_wait();
    while (true)
    {
        ServiceCode status = VirtioBlkChannelIO__poll(event);
        if (status != SERVICE_EVENT_QUEUE_EMPTY)
        {
            return status;
        }
        Channel__wait_event();
    }
}

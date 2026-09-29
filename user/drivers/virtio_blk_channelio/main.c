#include "channelio.h"
#include "runtime.h"

#include "ipc/ipc_platform.h"

static ServiceCode VirtioBlkProcess__subscribe(Anything context, IRQNumber irq)
{
    return VirtioBlkChannelIO__subscribe(irq);
}

static ServiceCode VirtioBlkProcess__rearm(Anything context, IRQNumber irq)
{
    return VirtioBlkChannelIO__acknowledge(irq);
}

int main(int argc, char ** argv)
{
    if (argc < 3 || !service_ok(service_become_service()))
    {
        return 1;
    }

    BString slot_argument = BString__new(argv[1]);
    uint64_t slot;
    match_optional(BString__parse_uint64(&slot_argument),
        some(value)
        {
            slot = value;
        },
        none()
        {
            return 1;
        }
    );

    BString path = BString__new(argv[2]);
    VirtioBlkRuntime runtime;
    VirtioBlkInterruptBackend interrupts =
        VirtioBlkInterruptBackend__new(0, VirtioBlkProcess__subscribe, VirtioBlkProcess__rearm);
    if (!service_ok(VirtioBlkRuntime__start(&runtime, slot, &path, &interrupts)))
    {
        return 1;
    }

    while (true)
    {
        Channel__prepare_wait();
        bool progress = false;
        ServiceEvent event;
        for (ServiceCode status = VirtioBlkChannelIO__poll(&event); status != SERVICE_EVENT_QUEUE_EMPTY;
             status = VirtioBlkChannelIO__poll(&event))
        {
            if (status == SERVICE_COMPLETE)
            {
                return 0;
            }
            if (!service_ok(status))
            {
                return 1;
            }
            if (event.type == SERVICE_EVENT_IRQ && event.source == runtime.device.irq_id)
            {
                Optional(InterruptDeliveryTiming) timing = InterruptClient__delivery_timing(event.data);
                match_optional(
                    timing,
                    some(delivery)
                    {
                        KernelNotification notification =
                            KernelNotification__new(event.source, event.data, delivery.kernel_irq_at_usec);
                        progress =
                            VirtioBlkRuntime__handle_irq(&runtime, &notification, delivery.manager_received_at_usec);
                    },
                    none_do_nothing()
                );
            }
        }

        match_result(VirtioBlkRuntime__step(&runtime),
            success_do_nothing(),
            error(code, message)
            {
                return 1;
            }
        );
        if (!progress && FiberScheduler__ready_count(&runtime.scheduler) == 0)
        {
            Channel__wait_event();
        }
    }
}

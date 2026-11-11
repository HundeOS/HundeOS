#include "driver.h"
#include "interrupts/interrupt_controller.h"
#include "ipc/ipc_platform.h"
#include "ipc/kernel_notification_queue.h"
#include "services/service_directory.h"
#include "sys/process.h"
#include "sys/service.h"

typedef struct RustDriverManager
{
    KernelNotificationQueuePtr events;
    Address distributor;
    Address registered_distributor;
} RustDriverManager;

typedef RustDriverManager * RustDriverManagerPtr;

static RustDriverManager RustDriverManager__new(ServiceInterruptManagerRegistrationPtr registration)
{
    return (RustDriverManager){.events = (KernelNotificationQueuePtr)registration->event_queue,
                               .distributor = registration->distributor,
                               .registered_distributor = registration->distributor};
}

static RustDriverEvent RustDriverEvent__new(HundeStatusResult status, uint64_t irq, uint64_t sequence,
                                            uint64_t kernel_at_usec, uint64_t manager_at_usec)
{
    return (RustDriverEvent){.status = status,
                             .irq = irq,
                             .sequence = sequence,
                             .kernel_at_usec = kernel_at_usec,
                             .manager_at_usec = manager_at_usec};
}

static RustDriverRequest RustDriverRequest__new(HundeStatusResult status, uint32_t kind, uint64_t irq,
                                                uint64_t generation, uint64_t sequence)
{
    return (RustDriverRequest){
        .status = status, .kind = kind, .irq = irq, .generation = generation, .sequence = sequence};
}

static InterruptProtocolResponse InterruptProtocolResponse__new(uint32_t kind, int32_t code, uint64_t irq,
                                                                uint64_t generation, uint64_t sequence,
                                                                uint64_t kernel_at_usec, uint64_t manager_at_usec)
{
    return (InterruptProtocolResponse){.kind = kind,
                                       .code = code,
                                       .irq = irq,
                                       .generation = generation,
                                       .sequence = sequence,
                                       .kernel_irq_at_usec = kernel_at_usec,
                                       .manager_received_at_usec = manager_at_usec};
}

RustNativeLayout hunde_driver_manager_layout(void)
{
    return RustNativeLayout__new(sizeof(RustDriverManager), _Alignof(RustDriverManager));
}

HundeStatusResult hunde_driver_manager_init(void * storage)
{
    RustDriverManagerPtr self = storage;
    ServiceInterruptManagerRegistration registration;
    ServiceCode code = service_register_interrupt_manager(&registration);
    if (!service_ok(code))
    {
        return RustNative__error(code);
    }
    if (registration.distributor == 0 || registration.distributor >= HUND_SERVICE_DIRECTORY_VADDR)
    {
        return RustNative__error(SERVICE_MAP_FAILED);
    }
    *self = RustDriverManager__new(&registration);
    return HundeStatusResult__success();
}

HundeStatusResult hunde_driver_route(void * storage, uint64_t pid, uint64_t irq)
{
    ServiceCode code = service_route_managed_irq(pid, irq);
    return service_ok(code) ? HundeStatusResult__success() : RustNative__error(code);
}

HundeStatusResult hunde_driver_route_self(void * storage, uint64_t irq)
{
    return hunde_driver_route(storage, process_current_pid(), irq);
}

static ServiceCode RustDriverManager__unmask(RustDriverManagerPtr self, uint64_t irq)
{
    if (irq >= IRQ_COUNT || self->distributor == 0 || self->distributor >= HUND_SERVICE_DIRECTORY_VADDR ||
        self->distributor != self->registered_distributor)
    {
        return SERVICE_MAP_FAILED;
    }
    InterruptController__unmask(self->distributor, irq);
    return SERVICE_OK;
}

HundeStatusResult hunde_driver_rearm(void * storage, uint64_t irq)
{
    ServiceCode code = RustDriverManager__unmask(storage, irq);
    return service_ok(code) ? HundeStatusResult__success() : RustNative__error(code);
}

RustDriverEvent hunde_driver_event(void * storage)
{
    RustDriverManagerPtr self = storage;
    match_optional(
        KernelNotificationQueue__pop(self->events),
        some(event)
        {
            return RustDriverEvent__new(HundeStatusResult__success(), event.id, event.sequence, event.occurred_at_usec,
                                        IpcPlatform__monotonic_usec());
        },
        none_do_nothing()
    );
    return RustDriverEvent__new(HundeStatusResult__new_pending(), 0, 0, 0, 0);
}

uint64_t hunde_driver_client_pid(void * storage)
{
    return ChannelServer__client_pid(storage);
}

RustDriverRequest hunde_driver_request(void * storage)
{
    ChannelServerPtr self = storage;
    match_result(
        ChannelServer__poll(self),
        success(optional_request)
        {
            match_optional(
                optional_request,
                some(metadata)
                {
                    InterruptProtocolRequest request;
                    if (metadata.size != sizeof(request) ||
                        !ChannelServer__read_request(self, (ChannelDataPtr)&request, sizeof(request)))
                    {
                        return RustDriverRequest__new(HundeStatusResult__success(), 0, 0, 0, 0);
                    }
                    return RustDriverRequest__new(HundeStatusResult__success(), request.kind, request.irq,
                                                  request.generation, request.sequence);
                },
                none_do_nothing()
            );
        },
        error(code, message)
        {
            return RustDriverRequest__new(RustNative__error(code), 0, 0, 0, 0);
        }
    );
    return RustDriverRequest__new(HundeStatusResult__new_pending(), 0, 0, 0, 0);
}

HundeStatusResult hunde_driver_reply(void * storage, uint32_t kind, int32_t code, uint64_t irq, uint64_t generation,
                                     uint64_t sequence, uint64_t kernel_at_usec, uint64_t manager_at_usec)
{
    InterruptProtocolResponse response =
        InterruptProtocolResponse__new(kind, code, irq, generation, sequence, kernel_at_usec, manager_at_usec);
    match_result(
        ChannelServer__reply(storage, (ConstChannelDataPtr)&response, sizeof(response)),
        success(status)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return code == CHANNEL_PENDING ? HundeStatusResult__new_pending() : RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

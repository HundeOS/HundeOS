#include "channel/server.h"
#include "interrupts/interrupt_controller.h"
#include "interrupts/interrupt_types.h"
#include "ipc/interrupt_protocol.h"
#include "ipc/kernel_notification_queue.h"
#include "services/service_directory.h"
#include "sys/io.h"
#include "sys/service.h"
#include "types/fixed_array.h"

typedef struct InterruptClient
{
    ChannelServer channel;
    InterruptProtocolResponse pending_response;
    bool pending;
} InterruptClient;
typedef InterruptClient * InterruptClientPtr;
OptionalDef(InterruptClient);

static InterruptClient InterruptClient__new(ChannelServerPtr channel)
{
    return (InterruptClient){.channel = *channel};
}

static bool InterruptClient__equals(void * first, void * second)
{
    InterruptClientPtr left = first;
    InterruptClientPtr right = second;
    return ChannelServer__connection(&left->channel) == ChannelServer__connection(&right->channel);
}

FixedArrayDefine(InterruptClient, QUEUE_LIMIT, InterruptClients, &InterruptClient__equals);
FixedArrayBodyDefine(InterruptClients);
typedef InterruptClients * InterruptClientsPtr;

typedef struct InterruptRegistration
{
    IRQNumber irq;
    QueueConnectionID connection;
    InterruptRegistrationGeneration generation;
    InterruptDeliverySequence outstanding;
    InterruptDeliverySequence pending_delivery;
    bool waiting;
} InterruptRegistration;
typedef InterruptRegistration * InterruptRegistrationPtr;
OptionalDef(InterruptRegistration);

static bool InterruptRegistration__equals(void * first, void * second)
{
    InterruptRegistrationPtr left = first;
    InterruptRegistrationPtr right = second;
    return left->irq == right->irq;
}

FixedArrayDefine(InterruptRegistration, IRQ_COUNT, InterruptRegistrations, &InterruptRegistration__equals);
FixedArrayBodyDefine(InterruptRegistrations);
typedef InterruptRegistrations * InterruptRegistrationsPtr;

typedef struct InterruptManager
{
    KernelNotificationQueuePtr events;
    Address distributor;
    QueueArrivalsPtr arrivals;
    InterruptClients clients;
    InterruptRegistrations registrations;
    InterruptRegistrationGeneration next_generation;
} InterruptManager;
typedef InterruptManager * InterruptManagerPtr;

static InterruptManager InterruptManager__new(ServiceInterruptManagerRegistrationPtr registration,
                                              QueueArrivalsPtr arrivals)
{
    return (InterruptManager){
        .events = (KernelNotificationQueuePtr)registration->event_queue,
        .distributor = registration->distributor,
        .arrivals = arrivals,
        .clients = InterruptClients__new(),
        .registrations = InterruptRegistrations__new(),
    };
}

static InterruptRegistration InterruptRegistration__new(IRQNumber irq, QueueConnectionID connection,
                                                        InterruptRegistrationGeneration generation)
{
    return (InterruptRegistration){.irq = irq, .connection = connection, .generation = generation};
}

static bool InterruptClient__matches_connection(InterruptClientPtr self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionIDPtr)context;
}

static bool InterruptClients__contains_connection(InterruptClientsPtr self, QueueConnectionID connection)
{
    match_find_result(
        InterruptClients__find(self, InterruptClient__matches_connection, &connection),
        some(client)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static bool InterruptRegistration__matches_irq(InterruptRegistrationPtr self, void * context)
{
    return self->irq == *(IRQNumberPtr)context;
}

static bool InterruptRegistrations__contains_irq(InterruptRegistrationsPtr self, IRQNumber irq)
{
    match_find_result(
        InterruptRegistrations__find(self, InterruptRegistration__matches_irq, &irq),
        some(registration)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static bool InterruptRegistration__matches_connection(InterruptRegistrationPtr self, void * context)
{
    return self->connection == *(QueueConnectionIDPtr)context;
}

static bool InterruptRegistrations__contains_connection(InterruptRegistrationsPtr self, QueueConnectionID connection)
{
    match_find_result(
        InterruptRegistrations__find(self, InterruptRegistration__matches_connection, &connection),
        some(registration)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static bool InterruptClient__send(InterruptClientPtr self, InterruptProtocolResponse response)
{
    match_result(
        ChannelServer__reply(&self->channel, (ConstChannelDataPtr)&response, sizeof(response)),
        success(status)
        {
            return true;
        },
        error(code, message)
        {
            if (code == CHANNEL_PENDING)
            {
                self->pending_response = response;
                self->pending = true;
                return true;
            }
            return false;
        }
    );
    return false;
}

static bool InterruptClient__flush(InterruptClientPtr self)
{
    if (!self->pending)
    {
        return false;
    }
    match_result(
        ChannelServer__reply(&self->channel, (ConstChannelDataPtr)&self->pending_response,
                             sizeof(self->pending_response)),
        success(status)
        {
            self->pending = false;
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

static void InterruptManager__subscribe(InterruptManagerPtr self, InterruptClientPtr client,
                                        ConstInterruptProtocolRequestPtr request)
{
    QueueConnectionID connection = ChannelServer__connection(&client->channel);
    if (InterruptRegistrations__contains_irq(&self->registrations, request->irq) ||
        InterruptRegistrations__contains_connection(&self->registrations, connection))
    {
        InterruptClient__send(client, InterruptProtocol__error(request->irq, INTERRUPT_PROTOCOL_ALREADY_REGISTERED));
        return;
    }
    ServiceCode route = service_route_managed_irq(ChannelServer__client_pid(&client->channel), request->irq);
    if (!service_ok(route))
    {
        InterruptProtocolCode code =
            route == SERVICE_PERMISSION_DENIED ? INTERRUPT_PROTOCOL_DENIED : INTERRUPT_PROTOCOL_INVALID;
        InterruptClient__send(client, InterruptProtocol__error(request->irq, code));
        return;
    }
    self->next_generation++;
    if (self->next_generation == 0)
    {
        self->next_generation++;
    }
    InterruptRegistration registration = InterruptRegistration__new(request->irq, connection, self->next_generation);
    match_result(
        InterruptRegistrations__push(&self->registrations, registration),
        success_do_nothing(),
        error(code, message)
        {
            InterruptClient__send(client, InterruptProtocol__error(request->irq, INTERRUPT_PROTOCOL_INVALID));
            return;
        }
    );
    InterruptClient__send(client, InterruptProtocol__subscribed(request->irq, registration.generation));
}

static void InterruptManager__wait(InterruptManagerPtr self, InterruptClientPtr client,
                                   ConstInterruptProtocolRequestPtr request)
{
    IRQNumber irq = request->irq;
    match_find_result_ptr(
        InterruptRegistrations__find(&self->registrations, InterruptRegistration__matches_irq, &irq),
        some(registration)
        {
            if (registration->connection == ChannelServer__connection(&client->channel) &&
                registration->generation == request->generation && registration->outstanding == 0 &&
                !registration->waiting)
            {
                registration->waiting = true;
                return;
            }
        },
        none_do_nothing()
    );
    InterruptClient__send(client, InterruptProtocol__error(request->irq, INTERRUPT_PROTOCOL_INVALID));
}

static void InterruptManager__acknowledge(InterruptManagerPtr self, InterruptClientPtr client,
                                          ConstInterruptProtocolRequestPtr request)
{
    IRQNumber irq = request->irq;
    match_find_result_ptr(
        InterruptRegistrations__find(&self->registrations, InterruptRegistration__matches_irq, &irq),
        some(registration)
        {
            if (registration->connection == ChannelServer__connection(&client->channel) &&
                registration->generation == request->generation && registration->outstanding == request->sequence &&
                registration->outstanding != 0)
            {
                registration->outstanding = 0;
                InterruptController__unmask(self->distributor, registration->irq);
                InterruptClient__send(
                    client, InterruptProtocol__acknowledged(request->irq, request->generation, request->sequence));
                return;
            }
        },
        none_do_nothing()
    );
    InterruptClient__send(client, InterruptProtocol__error(request->irq, INTERRUPT_PROTOCOL_STALE_ACKNOWLEDGEMENT));
}

static bool InterruptManager__handle_client(InterruptManagerPtr self, InterruptClientPtr client)
{
    bool work = InterruptClient__flush(client);
    if (client->pending)
    {
        return work;
    }
    match_result(
        ChannelServer__poll(&client->channel),
        success(optional_request)
        {
            match_optional(
                optional_request,
                some(metadata)
                {
                    InterruptProtocolRequest request;
                    if (metadata.size != sizeof(request) ||
                        !ChannelServer__read_request(&client->channel, (ChannelDataPtr)&request, sizeof(request)))
                    {
                        InterruptClient__send(client, InterruptProtocol__error(0, INTERRUPT_PROTOCOL_INVALID));
                        return true;
                    }
                    if (request.kind == INTERRUPT_PROTOCOL_SUBSCRIBE)
                    {
                        InterruptManager__subscribe(self, client, &request);
                    }
                    else if (request.kind == INTERRUPT_PROTOCOL_WAIT)
                    {
                        InterruptManager__wait(self, client, &request);
                    }
                    else if (request.kind == INTERRUPT_PROTOCOL_ACKNOWLEDGE)
                    {
                        InterruptManager__acknowledge(self, client, &request);
                    }
                    else
                    {
                        InterruptClient__send(client,
                                              InterruptProtocol__error(request.irq, INTERRUPT_PROTOCOL_INVALID));
                    }
                    return true;
                },
                none_do_nothing()
            );
        },
        error(code, message)
        {
            return work;
        }
    );
    return work;
}

static bool InterruptManager__accept_clients(InterruptManagerPtr self, QueueParticipantID warden_pid)
{
    bool work = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &self->arrivals->connections,
        index,
        connection,
        {
            if (connection == 0 || InterruptClients__contains_connection(&self->clients, connection))
            {
                continue;
            }
            match_result(
                ChannelServer__accept(connection, warden_pid),
                success(channel)
                {
                    InterruptClient client = InterruptClient__new(&channel);
                    match_result(
                        InterruptClients__push(&self->clients, client),
                        success_do_nothing(),
                        error(code, message)
                        {
                            ChannelServer__close(&channel);
                        }
                    );
                },
                error(code, message)
                {
                    QueueConnection__close(connection);
                }
            );
            work = true;
        }
    );
    return work;
}

static bool InterruptManager__deliver(InterruptManagerPtr self)
{
    match_optional(
        KernelNotificationQueue__pop(self->events),
        some(event)
        {
            match_find_result_ptr(
                InterruptRegistrations__find(&self->registrations, InterruptRegistration__matches_irq, &event.id),
                some(registration)
                {
                    registration->pending_delivery = event.sequence;
                },
                none_do_nothing()
            );
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static bool InterruptManager__deliver_pending(InterruptManagerPtr self, InterruptRegistrationPtr registration)
{
    if (registration->pending_delivery == 0 || registration->outstanding != 0 || !registration->waiting)
    {
        return false;
    }
    match_find_result_ptr(
        InterruptClients__find(&self->clients, InterruptClient__matches_connection, &registration->connection),
        some(client)
        {
            if (!client->pending &&
                InterruptClient__send(client, InterruptProtocol__delivery(registration->irq, registration->generation,
                                                                          registration->pending_delivery)))
            {
                registration->outstanding = registration->pending_delivery;
                registration->pending_delivery = 0;
                registration->waiting = false;
                return true;
            }
        },
        none_do_nothing()
    );
    return false;
}

static bool InterruptManager__step(InterruptManagerPtr self, QueueParticipantID warden_pid)
{
    bool work = InterruptManager__accept_clients(self, warden_pid);
    for (ArrayIndex index = 0; index < InterruptClients__len(&self->clients); index++)
    {
        match_optional(
            InterruptClients__ptr_at(&self->clients, index),
            some(client)
            {
                work = InterruptManager__handle_client(self, client) || work;
            },
            none_do_nothing()
        );
    }
    for (ArrayIndex index = 0; index < InterruptRegistrations__len(&self->registrations); index++)
    {
        match_optional(
            InterruptRegistrations__ptr_at(&self->registrations, index),
            some(registration)
            {
                work = InterruptManager__deliver_pending(self, registration) || work;
            },
            none_do_nothing()
        );
    }
    return InterruptManager__deliver(self) || work;
}

int main(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    ServiceInterruptManagerRegistration kernel_registration;
    if (!service_ok(service_register_interrupt_manager(&kernel_registration)))
    {
        return 1;
    }
    match_result(
        Queue__listen(&bstr(INTERRUPT_MANAGER_SERVICE_NAME)),
        success(arrivals)
        {
            if (!service_ok(service_publish_ready(&bstr(INTERRUPT_MANAGER_SERVICE_NAME))))
            {
                return 1;
            }
            InterruptManager manager = InterruptManager__new(&kernel_registration, arrivals);
            Channel__prepare_wait();
            while (true)
            {
                QueueParticipantID warden_pid = 0;
                match_optional(
                    ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(CHANNEL_WARDEN_SERVICE_NAME)),
                    some(pid)
                    {
                        warden_pid = pid;
                    },
                    none_do_nothing()
                );
                if (!InterruptManager__step(&manager, warden_pid))
                {
                    Channel__wait_event();
                }
            }
        },
        error(code, message)
        {
            IO__printf(&bstr("hund-unterbrict: listen failed %i\n"), (int)code);
        }
    );
    return 1;
}

#include "unity.h"

#define main hund_unterbrict_main_under_test
#define service_route_managed_irq fake_route_managed_irq
#define InterruptController__unmask fake_unmask_irq
#include "../../../../user/services/hund-unterbrict/main.c"

static QueueIdentity identity;
static QueuePage client_page;
static QueuePage server_page;
static IpcDoorbell client_doorbell;
static IpcDoorbell server_doorbell;
static Queue client_queue;
static KernelNotificationQueue event_queue;
static uint64_t routed_client_pid;
static IRQNumber routed_irq;
static Address unmasked_distributor;
static IRQNumber unmasked_irq;
static IRQNumber hosted_irq;
static uint64_t hosted_irq_at_usec;
static uint64_t hosted_manager_at_usec;

bool VirtioBlkRuntime__handle_irq(VirtioBlkRuntimePtr self, ConstKernelNotificationPtr event,
                                  uint64_t manager_received_at_usec)
{
    hosted_irq = event->id;
    hosted_irq_at_usec = event->occurred_at_usec;
    hosted_manager_at_usec = manager_received_at_usec;
    return true;
}

void setUp(void)
{
    identity = (QueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .accepted = 1};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
    event_queue = KernelNotificationQueue__new();
    routed_client_pid = 0;
    routed_irq = 0;
    unmasked_distributor = 0;
    unmasked_irq = 0;
    hosted_irq = 0;
    hosted_irq_at_usec = 0;
    hosted_manager_at_usec = 0;
    client_doorbell = (IpcDoorbell){0};
    server_doorbell = (IpcDoorbell){0};
    client_queue = unwrap_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 4, &server_doorbell, &Queue__ignore_data_available),
        (Queue){0});
}

void test_interrupt_manager_dispatches_hosted_irq_without_client_relay(void)
{
    ServiceInterruptManagerRegistration kernel_registration = {
        .event_queue = (Address)&event_queue,
        .distributor = 0x1000,
    };
    InterruptManager manager = InterruptManager__new(&kernel_registration, 0);
    VirtioBlkRuntime runtime = {0};
    runtime.device.irq_id = 51;
    match_result(HostedBlockDrivers__push(&manager.block_drivers, &runtime),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(KernelNotificationQueue__publish(&event_queue, 51, 7, 1234),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_TRUE(InterruptManager__deliver(&manager));
    TEST_ASSERT_EQUAL_UINT64(51, hosted_irq);
    TEST_ASSERT_EQUAL_UINT64(1234, hosted_irq_at_usec);
    TEST_ASSERT_GREATER_THAN_UINT64(hosted_irq_at_usec, hosted_manager_at_usec);
    TEST_ASSERT_EQUAL_UINT64(0, InterruptRegistrations__len(&manager.registrations));
}

void tearDown(void)
{
}

static InterruptClient test_client(void)
{
    Queue server_queue = unwrap_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 5, &client_doorbell, &Queue__ignore_data_available),
        (Queue){0});
    ChannelServer channel = {
        .control = server_queue,
        .token = 29,
        .client_pid = 4,
        .request_size = sizeof(InterruptProtocolRequest),
        .handling_request = true,
    };
    return InterruptClient__new(&channel);
}

static void prepare_request(InterruptClientPtr client, ConstInterruptProtocolRequestPtr request)
{
    TEST_ASSERT_TRUE(Queue__write_data(&client_queue, (ConstChannelDataPtr)request, sizeof(*request), 0));
    client->channel.request_size = sizeof(*request);
    client->channel.handling_request = true;
}

static InterruptProtocolResponse take_response(void)
{
    match_result(
        Queue__pop(&client_queue),
        success(completion)
        {
            TEST_ASSERT_EQUAL_UINT64(CHANNEL_RESPONSE_REPLIED, completion.kind);
            InterruptProtocolResponse response;
            TEST_ASSERT_TRUE(Queue__read_data(&client_queue, (ChannelDataPtr)&response, sizeof(response)));
            return response;
        },
        error_test_fail()
    );
    return (InterruptProtocolResponse){0};
}

ServiceCode fake_route_managed_irq(uint64_t client_pid, uint64_t irq)
{
    routed_client_pid = client_pid;
    routed_irq = irq;
    return SERVICE_OK;
}

void fake_unmask_irq(Address distributor, IRQNumber irq)
{
    unmasked_distributor = distributor;
    unmasked_irq = irq;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_register_interrupt_manager(ServiceInterruptManagerRegistrationPtr registration)
{
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name)
{
    return Error(QueueArrivalsPtr, bstr("not used"), QUEUE_INVALID);
}

Result(Queue) Queue__accept(QueueConnectionID connection)
{
    return Error(Queue, bstr("not used"), QUEUE_INVALID);
}

QueueCode QueueConnection__close(QueueConnectionID connection)
{
    return QUEUE_OK;
}

void Queue__prepare_wait(void)
{
}

void Queue__wait_event(void)
{
}

int IO__printf(ConstBStringPtr format, ...)
{
    return 0;
}

void test_interrupt_manager_routes_channel_client_delivers_and_acknowledges(void)
{
    InterruptClient client = test_client();
    ServiceInterruptManagerRegistration kernel_registration = {
        .event_queue = (Address)&event_queue,
        .distributor = 0x1000,
    };
    InterruptManager manager = InterruptManager__new(&kernel_registration, 0);
    InterruptProtocolRequest subscribe = InterruptProtocol__subscribe(48);

    InterruptManager__subscribe(&manager, &client, &subscribe);
    TEST_ASSERT_EQUAL_UINT64(4, routed_client_pid);
    TEST_ASSERT_EQUAL_UINT64(48, routed_irq);
    InterruptProtocolResponse subscribed = take_response();
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_SUBSCRIBED, subscribed.kind);
    TEST_ASSERT_EQUAL_UINT64(1, subscribed.generation);

    match_result(InterruptClients__push(&manager.clients, client),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(
        InterruptClients__ptr_at(&manager.clients, 0),
        some(stored_client)
        {
            InterruptProtocolRequest wait = InterruptProtocol__wait(48, subscribed.generation);
            prepare_request(stored_client, &wait);
            InterruptManager__wait(&manager, stored_client, &wait);
        },
        none_test_fail()
    );

    match_result(KernelNotificationQueue__publish(&event_queue, 48, 9, 1234),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(InterruptManager__deliver(&manager));
    match_find_result_ptr(
        InterruptRegistrations__find(&manager.registrations, InterruptRegistration__matches_irq, &routed_irq),
        some(registration)
        {
            TEST_ASSERT_TRUE(InterruptManager__deliver_pending(&manager, registration));
        },
        none_test_fail()
    );
    InterruptProtocolResponse delivery = take_response();
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_DELIVERY, delivery.kind);
    TEST_ASSERT_EQUAL_UINT64(9, delivery.sequence);
    TEST_ASSERT_EQUAL_UINT64(1234, delivery.kernel_irq_at_usec);
    TEST_ASSERT_GREATER_THAN_UINT64(delivery.kernel_irq_at_usec, delivery.manager_received_at_usec);

    match_optional(
        InterruptClients__ptr_at(&manager.clients, 0),
        some(stored_client)
        {
            InterruptProtocolRequest acknowledgement =
                InterruptProtocol__acknowledgement(48, delivery.generation, delivery.sequence);
            prepare_request(stored_client, &acknowledgement);
            InterruptManager__acknowledge(&manager, stored_client, &acknowledgement);
        },
        none_test_fail()
    );
    InterruptProtocolResponse acknowledged = take_response();
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_ACKNOWLEDGED, acknowledged.kind);
    TEST_ASSERT_EQUAL_HEX64(0x1000, unmasked_distributor);
    TEST_ASSERT_EQUAL_UINT64(48, unmasked_irq);
}

void test_interrupt_manager_refuses_changed_distributor_without_rearming(void)
{
    InterruptClient client = test_client();
    ServiceInterruptManagerRegistration kernel_registration = {
        .event_queue = (Address)&event_queue,
        .distributor = 0x1000,
    };
    InterruptManager manager = InterruptManager__new(&kernel_registration, 0);
    InterruptProtocolRequest subscribe = InterruptProtocol__subscribe(48);
    InterruptManager__subscribe(&manager, &client, &subscribe);
    InterruptProtocolResponse subscribed = take_response();

    match_result(InterruptClients__push(&manager.clients, client),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(KernelNotificationQueue__publish(&event_queue, 48, 9, 1234),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(InterruptManager__deliver(&manager));
    match_find_result_ptr(
        InterruptRegistrations__find(&manager.registrations, InterruptRegistration__matches_irq, &routed_irq),
        some(registration)
        {
            registration->outstanding = 9;
        },
        none_test_fail()
    );

    manager.distributor = HUND_SERVICE_DIRECTORY_VADDR;
    match_optional(
        InterruptClients__ptr_at(&manager.clients, 0),
        some(stored_client)
        {
            InterruptProtocolRequest acknowledgement = InterruptProtocol__acknowledgement(48, subscribed.generation, 9);
            prepare_request(stored_client, &acknowledgement);
            InterruptManager__acknowledge(&manager, stored_client, &acknowledgement);
        },
        none_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_ERROR, take_response().kind);
    TEST_ASSERT_EQUAL_HEX64(0, unmasked_distributor);
    TEST_ASSERT_EQUAL_UINT64(0, unmasked_irq);
}

void test_interrupt_manager_rejects_stale_channel_acknowledgement_without_rearming(void)
{
    InterruptClient client = test_client();
    ServiceInterruptManagerRegistration kernel_registration = {
        .event_queue = (Address)&event_queue,
        .distributor = 0x1000,
    };
    InterruptManager manager = InterruptManager__new(&kernel_registration, 0);
    InterruptProtocolRequest subscribe = InterruptProtocol__subscribe(48);
    InterruptManager__subscribe(&manager, &client, &subscribe);
    take_response();

    InterruptProtocolRequest stale = InterruptProtocol__acknowledgement(48, 1, 7);
    prepare_request(&client, &stale);
    InterruptManager__acknowledge(&manager, &client, &stale);

    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_ERROR, take_response().kind);
    TEST_ASSERT_EQUAL_UINT64(0, unmasked_irq);
}

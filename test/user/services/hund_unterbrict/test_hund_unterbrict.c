#include "unity.h"

#define main hund_unterbrict_main_under_test
#define service_route_managed_irq fake_route_managed_irq
#define InterruptController__unmask fake_unmask_irq
#include "../../../../user/services/hund-unterbrict/main.c"

static QueueIdentity identity;
static QueuePage client_page;
static QueuePage server_page;
static Queue client_queue;
static ChannelPayload payload;
static KernelNotificationQueue event_queue;
static uint64_t routed_client_pid;
static IRQNumber routed_irq;
static IRQNumber unmasked_irq;

void setUp(void)
{
    identity = (QueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .accepted = 1};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
    payload = (ChannelPayload){0};
    event_queue = KernelNotificationQueue__new();
    routed_client_pid = 0;
    routed_irq = 0;
    unmasked_irq = 0;
    client_queue = unwrap_result(Queue__attach(&identity, &client_page, &server_page, 17, 4), (Queue){0});
}

void tearDown(void)
{
}

static InterruptClient test_client(void)
{
    Queue server_queue = unwrap_result(Queue__attach(&identity, &client_page, &server_page, 17, 5), (Queue){0});
    ChannelServer channel = {
        .control = server_queue,
        .payload = &payload,
        .token = 29,
        .client_pid = 4,
        .request_size = sizeof(InterruptProtocolRequest),
        .handling_request = true,
    };
    return InterruptClient__new(&channel);
}

static void prepare_request(InterruptClientPtr client, ConstInterruptProtocolRequestPtr request)
{
    TEST_ASSERT_TRUE(ChannelPayload__write_request(&payload, (ConstChannelDataPtr)request, sizeof(*request)));
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
            TEST_ASSERT_TRUE(ChannelPayload__read_reply(&payload, (ChannelDataPtr)&response, sizeof(response)));
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
    unmasked_irq = irq;
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

    match_result(KernelNotificationQueue__publish(&event_queue, 48, 9),
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
    TEST_ASSERT_EQUAL_UINT64(48, unmasked_irq);
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

#include "unity.h"

#define service_route_managed_irq fake_route_managed_irq
#define InterruptController__unmask fake_unmask_irq
#include "../../../../user/lib/hunde/driver.c"

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

void setUp(void)
{
    identity = (QueueIdentity){.connection = 17, .client_pid = 4, .server_pid = 5, .origin_pid = 4, .accepted = 1};
    client_page = (QueuePage){0};
    server_page = (QueuePage){0};
    event_queue = KernelNotificationQueue__new();
    routed_client_pid = 0;
    routed_irq = 0;
    unmasked_distributor = 0;
    unmasked_irq = 0;
    client_doorbell = (IpcDoorbell){0};
    server_doorbell = (IpcDoorbell){0};
    client_queue = unwrap_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 4, &server_doorbell, &Queue__ignore_data_available),
        (Queue){0});
}

void tearDown(void)
{
}

static RustDriverManager test_manager(void)
{
    ServiceInterruptManagerRegistration registration = {.event_queue = (Address)&event_queue, .distributor = 0x1000};
    return RustDriverManager__new(&registration);
}

static ChannelServer test_client(void)
{
    Queue server_queue = unwrap_result(
        Queue__attach(&identity, &client_page, &server_page, 17, 5, &client_doorbell, &Queue__ignore_data_available),
        (Queue){0});
    return (ChannelServer){.control = server_queue,
                           .token = 29,
                           .client_pid = 4,
                           .request_size = sizeof(InterruptProtocolRequest),
                           .handling_request = true};
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
    return InterruptProtocol__error(0, INTERRUPT_PROTOCOL_INVALID);
}

void test_driver_boundary_preserves_irq_identity_sequence_and_timestamps(void)
{
    RustDriverManager manager = test_manager();
    match_result(KernelNotificationQueue__publish(&event_queue, 51, 7, 1234),
        success_do_nothing(),
        error_test_fail()
    );
    RustDriverEvent event = hunde_driver_event(&manager);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_SUCCESS, event.status.kind);
    TEST_ASSERT_EQUAL_UINT64(51, event.irq);
    TEST_ASSERT_EQUAL_UINT64(7, event.sequence);
    TEST_ASSERT_EQUAL_UINT64(1234, event.kernel_at_usec);
    TEST_ASSERT_GREATER_THAN_UINT64(1234, event.manager_at_usec);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_IO_PENDING, hunde_driver_event(&manager).status.kind);
}

void test_driver_boundary_reads_authenticated_identity_after_polling_the_request(void)
{
    ChannelServer client = test_client();
    client.handling_request = false;
    client.client_pid = 0;
    InterruptProtocolRequest request = InterruptProtocol__subscribe(48);
    TEST_ASSERT_TRUE(Queue__write_data(&client_queue, (ConstChannelDataPtr)&request, sizeof(request), 0));
    QueueMessage message = ChannelControlMessage__new(CHANNEL_REQUEST_SUBMIT, 0, sizeof(request), 17);
    message.request = (RequestMetadata){.generation = 1, .origin_pid = 4};
    match_result(Queue__push(&client_queue, &message),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0, hunde_driver_client_pid(&client));
    RustDriverRequest decoded = hunde_driver_request(&client);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_SUCCESS, decoded.status.kind);
    TEST_ASSERT_EQUAL_UINT32(INTERRUPT_PROTOCOL_SUBSCRIBE, decoded.kind);
    TEST_ASSERT_EQUAL_UINT64(48, decoded.irq);
    TEST_ASSERT_EQUAL_UINT64(4, hunde_driver_client_pid(&client));
}

void test_driver_boundary_routes_authenticated_pid_and_preserves_response_fields(void)
{
    RustDriverManager manager = test_manager();
    ChannelServer client = test_client();
    TEST_ASSERT_EQUAL_UINT64(4, hunde_driver_client_pid(&client));
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_SUCCESS,
                             hunde_driver_route(&manager, hunde_driver_client_pid(&client), 48).kind);
    TEST_ASSERT_EQUAL_UINT64(4, routed_client_pid);
    TEST_ASSERT_EQUAL_UINT64(48, routed_irq);
    TEST_ASSERT_EQUAL_UINT32(
        HUNDE_ABI_SUCCESS,
        hunde_driver_reply(&client, INTERRUPT_PROTOCOL_DELIVERY, INTERRUPT_PROTOCOL_OK, 48, 2, 9, 100, 110).kind);
    InterruptProtocolResponse response = take_response();
    TEST_ASSERT_EQUAL_UINT32(INTERRUPT_PROTOCOL_DELIVERY, response.kind);
    TEST_ASSERT_EQUAL_UINT64(48, response.irq);
    TEST_ASSERT_EQUAL_UINT64(2, response.generation);
    TEST_ASSERT_EQUAL_UINT64(9, response.sequence);
    TEST_ASSERT_EQUAL_UINT64(100, response.kernel_irq_at_usec);
    TEST_ASSERT_EQUAL_UINT64(110, response.manager_received_at_usec);
}

void test_driver_boundary_refuses_changed_distributor_and_out_of_range_irq(void)
{
    RustDriverManager manager = test_manager();
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_SUCCESS, hunde_driver_rearm(&manager, 48).kind);
    TEST_ASSERT_EQUAL_UINT64(48, unmasked_irq);
    unmasked_irq = 0;
    manager.distributor = HUND_SERVICE_DIRECTORY_VADDR;
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_ERROR, hunde_driver_rearm(&manager, 48).kind);
    manager.distributor = manager.registered_distributor;
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_ERROR, hunde_driver_rearm(&manager, IRQ_COUNT).kind);
    TEST_ASSERT_EQUAL_UINT64(0, unmasked_irq);
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

ServiceCode service_register_interrupt_manager(ServiceInterruptManagerRegistrationPtr registration)
{
    return SERVICE_MAP_FAILED;
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

uint64_t process_current_pid(void)
{
    return 5;
}

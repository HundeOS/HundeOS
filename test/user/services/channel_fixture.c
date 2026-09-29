#ifndef TEST_CHANNEL_FIXTURE_C
#define TEST_CHANNEL_FIXTURE_C

#include <string.h>
#include "unity.h"
#include "sys/mem.h"

#define HUNDEKANAL_WARTER_ENTRYPOINT warden_main_under_test
#include "../../../user/services/hundekanal-warter/main.c"
#include "channel_echo_fixture.c"
#define CHANNEL_ECHO_CLIENT_ENTRYPOINT client_main_under_test
#include "../../../user/programs/channel_echo_client/main.c"

typedef struct TestTransport
{
    QueueIdentity identity;
    QueuePage client;
    QueuePage server;
    IpcDoorbell client_doorbell;
    IpcDoorbell server_doorbell;
    bool client_closed;
    bool server_closed;
} TestTransport;

static TestTransport transports[QUEUE_LIMIT];
static ChannelPayload payloads[QUEUE_LIMIT];
static bool pages_live[QUEUE_LIMIT];
static QueueArrivals warden_arrivals;
static QueueArrivals echo_arrivals;
static ChannelSessions exchanges;
static EchoConnections echoes;
static ServiceDirectory directory;
static QueueParticipantID current_pid;
static QueueHandleGeneration next_generation;
static uint64_t allocated_pages;
static uint64_t released_pages;
static uint64_t last_allocated_client;
static uint64_t waits;
static bool fail_allocation;
static bool fail_release;
static bool run_echo;
static bool connect_as_kernel;
static QueueParticipantID kernel_origin_pid;
static void (*fixture_server_step)(void);
static bool fixture_listener_available;
static char output[4096];
static size_t output_length;
static void ChannelFixture__pump(void);

static void ChannelFixture__push(QueuePtr queue, ConstQueueMessagePtr message)
{
    match_result(
        Queue__push(queue, message),
        success(status)
        {
            TEST_ASSERT_EQUAL(QUEUE_OK, status);
        },
        error_test_fail()
    );
}

static void ChannelFixture__reset(void)
{
    memset(transports, 0, sizeof(transports));
    memset(payloads, 0, sizeof(payloads));
    memset(pages_live, 0, sizeof(pages_live));
    warden_arrivals = (QueueArrivals){.endpoint = 16};
    echo_arrivals = (QueueArrivals){.endpoint = 33};
    exchanges = ChannelSessions__new();
    echoes = EchoConnections__new();
    directory = (ServiceDirectory){0};
    directory.entries[0].present = 1;
    directory.entries[0].service_pid = 7;
    directory.entries[0].generation = warden_arrivals.endpoint;
    directory.entries[0].name = bstr("hundekanal-warter");
    directory.entries[1].present = 1;
    directory.entries[1].service_pid = 8;
    directory.entries[1].generation = echo_arrivals.endpoint;
    directory.entries[1].name = bstr("hundekanal-test-echo");
    current_pid = 42;
    next_generation = 4;
    allocated_pages = 0;
    released_pages = 0;
    last_allocated_client = 0;
    waits = 0;
    fail_allocation = false;
    fail_release = false;
    run_echo = true;
    connect_as_kernel = false;
    kernel_origin_pid = 0;
    fixture_server_step = NULL;
    fixture_listener_available = false;
    output_length = 0;
    output[0] = '\0';
}

uint64_t process_current_pid(void)
{
    return current_pid;
}

void Queue__prepare_wait(void)
{
}

void Queue__wait_event(void)
{
    ChannelFixture__pump();
}

void IO__print(ConstBStringPtr str)
{
    for (StringPos index = 0; index < BString__length(str); index++)
    {
        TEST_ASSERT_LESS_THAN(sizeof(output) - 1, output_length);
        output[output_length++] = BString__char_at_or(str, index, '\0');
    }
    output[output_length] = '\0';
}

int IO__printf(ConstBStringPtr format, ...)
{
    return 0;
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}

Result(QueueArrivalsPtr) Queue__listen(ConstBStringPtr name)
{
    if (fixture_listener_available && BString__equals(name, &directory.entries[1].name))
    {
        return Success(QueueArrivalsPtr, &echo_arrivals);
    }
    return Error(QueueArrivalsPtr, bstr("Already listening"), QUEUE_OCCUPIED);
}

Result(Queue) Queue__connect(QueueEndpointID endpoint)
{
    QueueArrivalsPtr arrivals = &warden_arrivals;
    QueueParticipantID server = 7;
    if (endpoint == echo_arrivals.endpoint)
    {
        arrivals = &echo_arrivals;
        server = 8;
    }
    else if (endpoint != warden_arrivals.endpoint)
    {
        return Error(Queue, bstr("Stale endpoint"), QUEUE_INVALID);
    }
    for (uint64_t index = 0; index < QUEUE_LIMIT; index++)
    {
        TestTransport * transport = &transports[index];
        if (transport->identity.connection != 0 && !(transport->client_closed && transport->server_closed))
        {
            continue;
        }
        *transport = (TestTransport){0};
        QueueParticipantID client = connect_as_kernel ? QUEUE_KERNEL_PID : current_pid;
        QueueParticipantID origin = connect_as_kernel ? kernel_origin_pid : current_pid;
        transport->identity = (QueueIdentity){.connection = ++next_generation * QUEUE_LIMIT + index,
                                              .endpoint = endpoint,
                                              .client_pid = client,
                                              .server_pid = server,
                                              .origin_pid = origin};
        match_result(QueueConnectionSlots__set(&arrivals->connections, index, transport->identity.connection),
                     success_do_nothing(),
            error_test_fail()
        );
        arrivals->epoch++;
        return Queue__attach(&transport->identity, &transport->client, &transport->server,
                             transport->identity.connection, client, &transport->server_doorbell,
                             &Queue__ignore_data_available);
    }
    return Error(Queue, bstr("Full"), QUEUE_FULL);
}

Result(Queue) Queue__accept(QueueConnectionID id)
{
    TestTransport * transport = &transports[id % QUEUE_LIMIT];
    if (transport->identity.connection != id || transport->identity.server_pid != current_pid)
    {
        return Error(Queue, bstr("Denied"), QUEUE_DENIED);
    }
    if (transport->identity.closed)
    {
        return Error(Queue, bstr("Closed"), QUEUE_CLOSED);
    }
    transport->identity.accepted = 1;
    return Queue__attach(&transport->identity, &transport->client, &transport->server, id, current_pid,
                         &transport->client_doorbell, &Queue__ignore_data_available);
}

QueueCode QueueConnection__close(QueueConnectionID id)
{
    TestTransport * transport = &transports[id % QUEUE_LIMIT];
    TEST_ASSERT_EQUAL_UINT64(id, transport->identity.connection);
    transport->identity.closed = 1;
    if (current_pid == transport->identity.client_pid)
    {
        transport->client_closed = true;
    }
    else
    {
        TEST_ASSERT_EQUAL_UINT64(current_pid, transport->identity.server_pid);
        transport->server_closed = true;
        QueueArrivalsPtr arrivals = current_pid == 7 ? &warden_arrivals : &echo_arrivals;
        match_result(QueueConnectionSlots__set(&arrivals->connections, id % QUEUE_LIMIT, 0),
            success_do_nothing(),
                     error_test_fail()
        );
    }
    return QUEUE_OK;
}

Result(Anything) mem_alloc_shared_page(uint64_t first_peer, uint64_t second_peer)
{
    TEST_ASSERT_EQUAL_UINT64(7, current_pid);
    TEST_ASSERT_EQUAL_UINT64(8, second_peer);
    last_allocated_client = first_peer;
    if (fail_allocation)
    {
        return Error(Anything, bstr("Allocation failed"), MEM_ALLOCATION_FAILED);
    }
    for (uint64_t index = 0; index < QUEUE_LIMIT; index++)
    {
        if (pages_live[index])
        {
            continue;
        }
        pages_live[index] = true;
        payloads[index] = (ChannelPayload){0};
        allocated_pages++;
        return Success(Anything, (Anything)&payloads[index]);
    }
    return Error(Anything, bstr("Full"), MEM_ALLOCATION_FAILED);
}

MemStatus mem_release_shared_page(uint64_t address)
{
    TEST_ASSERT_EQUAL_UINT64(7, current_pid);
    if (fail_release)
    {
        return MEM_FREE_FAILED;
    }
    for (uint64_t index = 0; index < QUEUE_LIMIT; index++)
    {
        if (address != (uint64_t)&payloads[index])
        {
            continue;
        }
        TEST_ASSERT_TRUE(pages_live[index]);
        pages_live[index] = false;
        released_pages++;
        memset(&payloads[index], 0xdd, sizeof(ChannelPayload));
        return MEM_OK;
    }
    TEST_FAIL_MESSAGE("Unknown managed page");
    return MEM_FREE_FAILED;
}

static void ChannelFixture__pump(void)
{
    uint64_t caller = current_pid;
    current_pid = 7;
    ChannelSessions__step(&exchanges, &warden_arrivals);
    if (run_echo)
    {
        current_pid = 8;
        if (fixture_server_step != NULL)
        {
            fixture_server_step();
        }
        else
        {
            EchoConnections__step(&echoes, &echo_arrivals, 7);
        }
    }
    current_pid = caller;
}

void Queue__wait_writable(QueuePtr queue)
{
    while (Queue__status(queue) == QUEUE_PENDING)
    {
        TEST_ASSERT_LESS_THAN_UINT64(1000, ++waits);
        ChannelFixture__pump();
    }
}

void Queue__wait_readable(QueuePtr queue)
{
    while (Queue__status(queue) != QUEUE_CLOSED && queue->incoming->produced == queue->read)
    {
        TEST_ASSERT_LESS_THAN_UINT64(1000, ++waits);
        ChannelFixture__pump();
    }
}

static Queue ChannelFixture__client(void)
{
    Result(Queue) result = Queue__connect(warden_arrivals.endpoint);
    Queue queue = unwrap_result(result, (Queue){0});
    Queue__wait_writable(&queue);
    return queue;
}

static Queue ChannelFixture__kernel_client(QueueParticipantID origin_pid)
{
    connect_as_kernel = true;
    kernel_origin_pid = origin_pid;
    Result(Queue) result = Queue__connect(warden_arrivals.endpoint);
    connect_as_kernel = false;
    Queue queue = unwrap_result(result, (Queue){0});
    Queue__wait_writable(&queue);
    return queue;
}

static QueueMessage ChannelFixture__receive(QueuePtr queue)
{
    Queue__wait_readable(queue);
    match_result(Queue__pop(queue),
        success(message)
        {
            return message;
        },
        error_test_fail()
    );
    return (QueueMessage){0};
}

static QueueMessage ChannelFixture__open(QueuePtr queue)
{
    QueueMessage open = ChannelControlMessage__new(CHANNEL_REQUEST_OPEN, echo_arrivals.endpoint, 0, queue->connection);
    open.sender_pid = 9999;
    ChannelFixture__push(queue, &open);
    return ChannelFixture__receive(queue);
}

static void ChannelFixture__drain(void)
{
    for (size_t tick = 0; tick < 16; tick++)
    {
        ChannelFixture__pump();
    }
}

ChannelSignal ChannelSignal__new(ChannelSignalStatePtr state)
{
    return (ChannelSignal){.state = state};
}

FiberScheduler FiberScheduler__new(void)
{
    return (FiberScheduler){0};
}

Result(FiberPtr) FiberScheduler__spawn(FiberSchedulerPtr self, FiberEntry entry, Anything argument)
{
    return Error(FiberPtr, bstr("Not running real fibres in fixture"), FIBER_QUEUE_FULL);
}

Result(FiberCode) FiberScheduler__run_ready_until_blocked(FiberSchedulerPtr self)
{
    return Success(FiberCode, FIBER_OK);
}

Result(FiberCode) FiberScheduler__await_channel_signal(FiberSchedulerPtr self, ChannelSignalPtr channel)
{
    return Success(FiberCode, FIBER_OK);
}

Result(FiberCode) FiberScheduler__wake_channel_signal(FiberSchedulerPtr self, ChannelSignalPtr channel)
{
    return Success(FiberCode, FIBER_OK);
}

#endif
#include "services/service_directory.h"

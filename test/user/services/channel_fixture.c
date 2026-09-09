#ifndef TEST_CHANNEL_FIXTURE_C
#define TEST_CHANNEL_FIXTURE_C

#include <string.h>
#include "unity.h"
#include "sys/mem.h"

#define HUNDEKANAL_WARTER_ENTRYPOINT warden_main_under_test
#include "../../../user/services/hundekanal-warter/main.c"
#define HUNDEKANAL_TEST_ECHO_ENTRYPOINT echo_main_under_test
#include "../../../user/services/hundekanal-test-echo/main.c"
#define CHANNEL_ECHO_CLIENT_ENTRYPOINT client_main_under_test
#include "../../../user/programs/channel_echo_client/main.c"

typedef struct TestTransport
{
    AtomicMessageQueueIdentity identity;
    AtomicMessageQueuePage client;
    AtomicMessageQueuePage server;
    bool client_closed;
    bool server_closed;
} TestTransport;

static TestTransport transports[ATOMIC_MESSAGE_QUEUE_LIMIT];
static ChannelPayload payloads[ATOMIC_MESSAGE_QUEUE_LIMIT];
static bool pages_live[ATOMIC_MESSAGE_QUEUE_LIMIT];
static AtomicMessageQueueArrivals warden_arrivals;
static AtomicMessageQueueArrivals echo_arrivals;
static ChannelExchanges exchanges;
static EchoConnections echoes;
static ServiceDirectory directory;
static AtomicMessageQueueParticipantID current_pid;
static AtomicMessageQueueHandleGeneration next_generation;
static uint64_t allocated_pages;
static uint64_t released_pages;
static uint64_t last_allocated_client;
static uint64_t waits;
static bool fail_allocation;
static bool fail_release;
static bool run_echo;
static char output[4096];
static size_t output_length;

static void ChannelFixture__push(AtomicMessageQueuePtr queue, ConstServiceQueueMessagePtr message)
{
    match_result(
        AtomicMessageQueue__push(queue, message),
        success(status) { TEST_ASSERT_EQUAL(ATOMIC_MESSAGE_QUEUE_OK, status); },
        error_test_fail()
    );
}

static void ChannelFixture__reset(void)
{
    memset(transports, 0, sizeof(transports));
    memset(payloads, 0, sizeof(payloads));
    memset(pages_live, 0, sizeof(pages_live));
    warden_arrivals = (AtomicMessageQueueArrivals){.endpoint = 16};
    echo_arrivals = (AtomicMessageQueueArrivals){.endpoint = 33};
    exchanges = ChannelExchanges__new();
    echoes = EchoConnections__new();
    directory = (ServiceDirectory){0};
    directory.entries[0].present = 1;
    directory.entries[0].service_pid = 7;
    directory.entries[0].generation = warden_arrivals.endpoint;
    ServiceDirectoryName__copy_from_bstring(&directory.entries[0].name, &bstr("hundekanal-warter"));
    directory.entries[1].present = 1;
    directory.entries[1].service_pid = 8;
    directory.entries[1].generation = echo_arrivals.endpoint;
    ServiceDirectoryName__copy_from_bstring(&directory.entries[1].name, &bstr("hundekanal-test-echo"));
    current_pid = 42;
    next_generation = 4;
    allocated_pages = 0;
    released_pages = 0;
    last_allocated_client = 0;
    waits = 0;
    fail_allocation = false;
    fail_release = false;
    run_echo = true;
    output_length = 0;
    output[0] = '\0';
}

uint64_t process_current_pid(void)
{
    return current_pid;
}
void ChannelExchange__prepare_wait(void)
{
}
void ChannelExchange__wait_event(void)
{
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

Result(AtomicMessageQueueArrivalsPtr) atomic_message_queue_listen(ConstBStringPtr name)
{
    return Error(AtomicMessageQueueArrivalsPtr, bstr("Already listening"), ATOMIC_MESSAGE_QUEUE_OCCUPIED);
}

Result(AtomicMessageQueue) atomic_message_queue_connect(AtomicMessageQueueEndpointID endpoint)
{
    AtomicMessageQueueArrivalsPtr arrivals = &warden_arrivals;
    AtomicMessageQueueParticipantID server = 7;
    if (endpoint == echo_arrivals.endpoint)
    {
        arrivals = &echo_arrivals;
        server = 8;
    }
    else if (endpoint != warden_arrivals.endpoint)
    {
        return Error(AtomicMessageQueue, bstr("Stale endpoint"), ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    for (uint64_t index = 0; index < ATOMIC_MESSAGE_QUEUE_LIMIT; index++)
    {
        TestTransport * transport = &transports[index];
        if (transport->identity.connection != 0 && !(transport->client_closed && transport->server_closed))
        {
            continue;
        }
        *transport = (TestTransport){0};
        transport->identity =
            (AtomicMessageQueueIdentity){.connection = ++next_generation * ATOMIC_MESSAGE_QUEUE_LIMIT + index,
                                         .endpoint = endpoint,
                                         .client_pid = current_pid,
                                         .server_pid = server};
        match_result(
            AtomicMessageQueueConnectionSlots__set(&arrivals->connections, index, transport->identity.connection),
            success_do_nothing(),
            error_test_fail()
        );
        arrivals->epoch++;
        return AtomicMessageQueue__attach(&transport->identity, &transport->client, &transport->server,
                                          transport->identity.connection, current_pid);
    }
    return Error(AtomicMessageQueue, bstr("Full"), ATOMIC_MESSAGE_QUEUE_FULL);
}

Result(AtomicMessageQueue) atomic_message_queue_accept(AtomicMessageQueueConnectionID id)
{
    TestTransport * transport = &transports[id % ATOMIC_MESSAGE_QUEUE_LIMIT];
    if (transport->identity.connection != id || transport->identity.server_pid != current_pid)
    {
        return Error(AtomicMessageQueue, bstr("Denied"), ATOMIC_MESSAGE_QUEUE_DENIED);
    }
    if (transport->identity.closed)
    {
        return Error(AtomicMessageQueue, bstr("Closed"), ATOMIC_MESSAGE_QUEUE_CLOSED);
    }
    transport->identity.accepted = 1;
    return AtomicMessageQueue__attach(&transport->identity, &transport->client, &transport->server, id, current_pid);
}

AtomicMessageQueueCode atomic_message_queue_close(AtomicMessageQueueConnectionID id)
{
    TestTransport * transport = &transports[id % ATOMIC_MESSAGE_QUEUE_LIMIT];
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
        AtomicMessageQueueArrivalsPtr arrivals = current_pid == 7 ? &warden_arrivals : &echo_arrivals;
        match_result(AtomicMessageQueueConnectionSlots__set(&arrivals->connections, id % ATOMIC_MESSAGE_QUEUE_LIMIT, 0),
                     success_do_nothing(),
            error_test_fail()
        );
    }
    return ATOMIC_MESSAGE_QUEUE_OK;
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
    for (uint64_t index = 0; index < ATOMIC_MESSAGE_QUEUE_LIMIT; index++)
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
    for (uint64_t index = 0; index < ATOMIC_MESSAGE_QUEUE_LIMIT; index++)
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
    ChannelExchanges__step(&exchanges, &warden_arrivals);
    if (run_echo)
    {
        current_pid = 8;
        EchoConnections__step(&echoes, &echo_arrivals, 7);
    }
    current_pid = caller;
}

void atomic_message_queue_wait_writable(AtomicMessageQueuePtr queue)
{
    while (AtomicMessageQueue__status(queue) == ATOMIC_MESSAGE_QUEUE_PENDING)
    {
        TEST_ASSERT_LESS_THAN_UINT64(1000, ++waits);
        ChannelFixture__pump();
    }
}
void atomic_message_queue_wait_readable(AtomicMessageQueuePtr queue)
{
    while (AtomicMessageQueue__status(queue) != ATOMIC_MESSAGE_QUEUE_CLOSED && queue->incoming->produced == queue->read)
    {
        TEST_ASSERT_LESS_THAN_UINT64(1000, ++waits);
        ChannelFixture__pump();
    }
}

static AtomicMessageQueue ChannelFixture__client(void)
{
    Result(AtomicMessageQueue) result = atomic_message_queue_connect(warden_arrivals.endpoint);
    AtomicMessageQueue queue = unwrap_result(result, (AtomicMessageQueue){0});
    atomic_message_queue_wait_writable(&queue);
    return queue;
}

static ServiceQueueMessage ChannelFixture__receive(AtomicMessageQueuePtr queue)
{
    atomic_message_queue_wait_readable(queue);
    match_result(AtomicMessageQueue__pop(queue),
        success(message) { return message; },
        error_test_fail()
    );
    return (ServiceQueueMessage){0};
}

static ServiceQueueMessage ChannelFixture__open(AtomicMessageQueuePtr queue)
{
    ServiceQueueMessage open =
        ChannelExchange__message(CHANNEL_REQUEST_OPEN, echo_arrivals.endpoint, 0, queue->connection);
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

Channel Channel__new(ChannelStatePtr state)
{
    return (Channel){.state = state};
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
Result(FiberCode) FiberScheduler__await_channel(FiberSchedulerPtr self, ChannelPtr channel)
{
    return Success(FiberCode, FIBER_OK);
}
Result(FiberCode) FiberScheduler__wake_channel(FiberSchedulerPtr self, ChannelPtr channel)
{
    return Success(FiberCode, FIBER_OK);
}

#endif

#ifndef TEST_CHANNEL_FIXTURE_C
#define TEST_CHANNEL_FIXTURE_C

#include <string.h>
#include "unity.h"
#include "sys/mem.h"
#include "sys/syslog.h"

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
    QueueConnectionID delegated_from;
} TestTransport;

static TestTransport transports[QUEUE_LIMIT];
static QueueArrivals warden_arrivals;
static QueueArrivals echo_arrivals;
static ChannelSessions exchanges;
static EchoConnections echoes;
static ServiceDirectory directory;
static QueueParticipantID current_pid;
static QueueHandleGeneration next_generation;
static uint64_t allocated_pages;
static uint64_t released_pages;
static uint64_t cancelled_delegations;
static uint64_t last_allocated_client;
static uint64_t waits;
static bool fail_allocation;
static bool fail_release;
static bool run_echo;
static bool connect_as_kernel;
static QueueParticipantID kernel_origin_pid;
static void (*fixture_server_step)(void);
static bool fixture_listener_available;
static bool fixture_wait_interrupted;
static char output[4096];
static size_t output_length;
static void ChannelFixture__pump(void);

static void ChannelFixture__record_notification(QueueConnectionID connection)
{
}

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
    cancelled_delegations = 0;
    last_allocated_client = 0;
    waits = 0;
    fail_allocation = false;
    fail_release = false;
    run_echo = true;
    connect_as_kernel = false;
    kernel_origin_pid = 0;
    fixture_server_step = NULL;
    fixture_listener_available = false;
    fixture_wait_interrupted = false;
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
                             &ChannelFixture__record_notification);
    }
    return Error(Queue, bstr("Full"), QUEUE_FULL);
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
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
                         &transport->client_doorbell, &ChannelFixture__record_notification);
}

Result(Queue) Queue__attach_existing(QueueConnectionID id)
{
    TestTransport * transport = &transports[id % QUEUE_LIMIT];
    if (transport->identity.connection != id)
    {
        return Error(Queue, bstr("Invalid connection"), QUEUE_INVALID);
    }
    IpcDoorbellPtr peer =
        current_pid == transport->identity.client_pid ? &transport->server_doorbell : &transport->client_doorbell;
    return Queue__attach(&transport->identity, &transport->client, &transport->server, id, current_pid, peer,
                         &ChannelFixture__record_notification);
}

Result(QueueConnectionID) Queue__delegate(QueueConnectionID client_connection, QueueEndpointID endpoint)
{
    TEST_ASSERT_EQUAL_UINT64(7, current_pid);
    TestTransport * control = &transports[client_connection % QUEUE_LIMIT];
    if (control->identity.connection != client_connection || control->identity.server_pid != current_pid ||
        !control->identity.accepted)
    {
        return Error(QueueConnectionID, bstr("Denied"), QUEUE_DENIED);
    }
    QueueParticipantID caller = current_pid;
    current_pid = control->identity.client_pid;
    connect_as_kernel = current_pid == QUEUE_KERNEL_PID;
    kernel_origin_pid = control->identity.origin_pid;
    Result(Queue) direct = Queue__connect(endpoint);
    connect_as_kernel = false;
    current_pid = caller;
    match_result(
        direct,
        success(queue)
        {
            TestTransport * delegated = &transports[Queue__connection(&queue) % QUEUE_LIMIT];
            delegated->identity.channel = 1;
            delegated->delegated_from = client_connection;
            return Success(QueueConnectionID, Queue__connection(&queue));
        },
        error(code, message)
        {
            return Error(QueueConnectionID, message, code);
        }
    );
    return Error(QueueConnectionID, bstr("Delegation failed"), QUEUE_INVALID);
}

QueueCode Queue__cancel_delegation(QueueConnectionID client_connection, QueueConnectionID delegated_connection)
{
    TEST_ASSERT_EQUAL_UINT64(7, current_pid);
    TestTransport * control = &transports[client_connection % QUEUE_LIMIT];
    TestTransport * delegated = &transports[delegated_connection % QUEUE_LIMIT];
    if (control->identity.connection != client_connection || control->identity.server_pid != current_pid ||
        delegated->identity.connection != delegated_connection || delegated->delegated_from != client_connection)
    {
        return QUEUE_DENIED;
    }
    delegated->identity.closed = 1;
    delegated->client_closed = true;
    delegated->server_closed = true;
    cancelled_delegations++;
    return QUEUE_OK;
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

bool Queue__wait_writable_interruptible(QueuePtr queue)
{
    if (fixture_wait_interrupted)
    {
        fixture_wait_interrupted = false;
        return false;
    }
    while (Queue__status(queue) == QUEUE_PENDING)
    {
        TEST_ASSERT_LESS_THAN_UINT64(1000, ++waits);
        ChannelFixture__pump();
    }
    return true;
}

bool Queue__wait_readable_interruptible(QueuePtr queue)
{
    if (fixture_wait_interrupted)
    {
        fixture_wait_interrupted = false;
        return false;
    }
    while (Queue__status(queue) != QUEUE_CLOSED && queue->incoming->produced == queue->read)
    {
        TEST_ASSERT_LESS_THAN_UINT64(1000, ++waits);
        ChannelFixture__pump();
    }
    return true;
}

void Queue__wait_writable(QueuePtr queue)
{
    Queue__wait_writable_interruptible(queue);
}

void Queue__wait_readable(QueuePtr queue)
{
    Queue__wait_readable_interruptible(queue);
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

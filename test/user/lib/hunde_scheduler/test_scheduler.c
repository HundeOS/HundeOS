#include "../../../hundetest.h"
#include "unity.h"
#include "../../../../user/lib/hunde/native.h"
#include "../../../../user/lib/fiber/scheduler.h"
#include "../../../../user/lib/sys/boot_info.h"
#include "../../../../user/lib/sys/mem.h"
#include "../../../../user/lib/sys/queue_wait.h"
#include "../../../../user/lib/sys/syscall.h"
#include "../../../../user/lib/buffer/client.h"
#include "syscalls/service.h"
#include <stdlib.h>

static uint64_t allocations;
static uint64_t releases;
static bool fail_allocation;
static unsigned process_waits;

typedef struct DelayedQueue
{
    QueueIdentity identity;
    QueuePage outgoing;
    QueuePage incoming;
    IpcDoorbell doorbell;
    Queue reader;
    Queue writer;
} DelayedQueue;

typedef DelayedQueue * DelayedQueuePtr;

static void DelayedQueue__init(DelayedQueuePtr self)
{
    *self = (DelayedQueue){0};
    self->identity = (QueueIdentity){.connection = 123, .client_pid = 1, .server_pid = 2, .accepted = 1};
    self->reader = Queue__new(&self->identity, &self->outgoing, &self->incoming, 123, 2, &self->doorbell,
                              Queue__ignore_data_available);
    self->writer = Queue__new(&self->identity, &self->incoming, &self->outgoing, 123, 1, &self->doorbell,
                              Queue__ignore_data_available);
}

static void DelayedQueue__publish(DelayedQueuePtr self)
{
    QueueMessage message = QueueMessage__new(2, 1);
    match_result(Queue__push(&self->writer, &message),
        success_do_nothing(),
        error_test_fail()
    );
}

static void DelayedQueue__receive(DelayedQueuePtr self)
{
    TEST_ASSERT_TRUE(QueueWait__has_adapter());
    TEST_ASSERT_TRUE(Queue__wait_readable_interruptible(&self->reader));
    match_result(Queue__pop(&self->reader),
        success_do_nothing(),
        error_test_fail()
    );
}

static DelayedQueue delayed_open;
static DelayedQueue delayed_reply;
static unsigned manager_opens;
static unsigned manager_submits;
static unsigned manager_closes;
static unsigned active_transactions;
static BufferID last_request;
static bool fail_submit;

SystemCallResult syscall(SystemCall number, Anything operation, Anything first, Anything second, Anything third,
                         Anything fourth)
{
    if (number == SERVICE_CALL && operation == SERVICE_QUEUE_WAIT_CONTROL)
    {
        return 0;
    }
    TEST_ASSERT_EQUAL(SERVICE_CALL, number);
    TEST_ASSERT_EQUAL(SERVICE_QUEUE_WAIT, operation);
    process_waits++;
    return SYSCALL_INTERRUPTED;
}

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr target)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(BUFFER_SERVICE_NAME), *target);
    manager_opens++;
    DelayedQueue__receive(&delayed_open);
    return Success(Channel, ((Channel){0}));
}

Result(ChannelReplySize)
    Channel__submit(ChannelPtr self, ConstChannelDataPtr request, size_t size, ChannelDataPtr response, size_t capacity)
{
    TEST_ASSERT_EQUAL_UINT64(sizeof(BufferRequest), size);
    TEST_ASSERT_EQUAL_UINT64(sizeof(BufferReply), capacity);
    TEST_ASSERT_EQUAL_UINT(0, active_transactions);
    active_transactions++;
    manager_submits++;
    last_request = ((ConstBufferRequestPtr)request)->id;
    if (fail_submit)
    {
        fail_submit = false;
        active_transactions--;
        return Error(ChannelReplySize, bstr("Peer closed"), QUEUE_CLOSED);
    }
    DelayedQueue__receive(&delayed_reply);
    active_transactions--;
    BufferReply reply = BufferReply__new(BUFFER_OK);
    __builtin_memcpy(response, &reply, sizeof(reply));
    return Success(ChannelReplySize, sizeof(reply));
}

ChannelCode Channel__close(ChannelPtr self)
{
    manager_closes++;
    return CHANNEL_OK;
}

int64_t BufferMemory__operation(MemOperation operation, uint64_t first, uint64_t second, uint64_t third,
                                uint64_t fourth)
{
    return BUFFER_DENIED;
}

Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    BootHardwareInfo info = BootHardwareInfo__new();
    info.default_user_stack_size = 65536;
    return Success(BootHardwareInfo, info);
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    if (fail_allocation)
    {
        return Error(Anything, bstr("Allocation failed"), FIBER_ALLOCATION_FAILED);
    }
    return Success(Anything, (Anything)malloc(size));
}

Result(Anything) mem_alloc_pages_result(size_t size)
{
    if (!fail_allocation)
    {
        allocations++;
    }
    return mem_alloc_bytes(size);
}

MemStatus mem_free_pages(void * address)
{
    releases++;
    free(address);
    return MEM_OK;
}

#include "../../../../user/lib/channel/signal.c"
#include "../../../../user/lib/fiber/fiber.c"
#include "../../../../user/lib/fiber/scheduler.c"

typedef struct TestTask
{
    void * scheduler;
    uint64_t visits;
    bool park;
} TestTask;

typedef TestTask * TestTaskPtr;

static TestTask TestTask__new(void * scheduler, bool park)
{
    return (TestTask){.scheduler = scheduler, .park = park};
}

static HundeStatusResult TestTask__enter(HundeHostContext context)
{
    TEST_ASSERT_TRUE(QueueWait__has_adapter());
    TestTaskPtr self = context;
    self->visits++;
    if (self->park)
    {
        HundeStatusResult result = hunde_rust_scheduler_park(self->scheduler);
        if (result.kind != HUNDE_ABI_SUCCESS)
        {
            return result;
        }
        self->visits++;
    }
    return HundeStatusResult__success();
}

static HundeStatusResult TestTask__other_entry(HundeHostContext context)
{
    TestTaskPtr self = context;
    self->visits += 10;
    return HundeStatusResult__success();
}

static HundeStatusResult TestTask__fail(HundeHostContext context)
{
    return RustNative__error(FIBER_INVALID_STATE);
}

static HundeStatusResult TestTask__complete(HundeHostContext context)
{
    TestTaskPtr self = context;
    self->visits++;
    return hunde_rust_scheduler_wake(self->scheduler);
}

static void * scheduler;

void setUp(void)
{
    allocations = releases = 0;
    fail_allocation = false;
    process_waits = manager_opens = manager_submits = manager_closes = active_transactions = 0;
    fail_submit = false;
    QueueWait__reset_after_fork();
    DelayedQueue__init(&delayed_open);
    DelayedQueue__init(&delayed_reply);
    RustNativeLayout layout = hunde_rust_scheduler_layout();
    scheduler = malloc(layout.size);
    hunde_rust_scheduler_init(scheduler);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(QueueWait__has_adapter());
    TEST_ASSERT_EQUAL_UINT(0, process_waits);
    TEST_ASSERT_EQUAL_UINT64(0, hunde_rust_scheduler_live(scheduler));
    TEST_ASSERT_EQUAL_UINT64(allocations, releases);
    free(scheduler);
}

void test_each_fibre_keeps_its_own_entry_and_context(void)
{
    TestTask first = TestTask__new(scheduler, true);
    TestTask second = TestTask__new(scheduler, false);
    RustFiberTask first_task = RustFiberTask__new(TestTask__enter, &first);
    RustFiberTask second_task = RustFiberTask__new(TestTask__other_entry, &second);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &first_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &second_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(1, first.visits);
    TEST_ASSERT_EQUAL_UINT64(10, second.visits);
    TEST_ASSERT_EQUAL_UINT64(1, hunde_rust_scheduler_waiting(scheduler));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_wake(scheduler).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(2, first.visits);
}

void test_event_wakes_every_parked_rust_fibre_and_no_waiters_is_success(void)
{
    TestTask first = TestTask__new(scheduler, true);
    TestTask second = TestTask__new(scheduler, true);
    RustFiberTask first_task = RustFiberTask__new(TestTask__enter, &first);
    RustFiberTask second_task = RustFiberTask__new(TestTask__enter, &second);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &first_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &second_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(2, hunde_rust_scheduler_waiting(scheduler));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_wake(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(0, hunde_rust_scheduler_waiting(scheduler));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(2, first.visits);
    TEST_ASSERT_EQUAL_UINT64(2, second.visits);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_wake(scheduler).kind);
}

void test_failed_spawn_does_not_replace_existing_work(void)
{
    TestTask first = TestTask__new(scheduler, false);
    TestTask second = TestTask__new(scheduler, false);
    RustFiberTask first_task = RustFiberTask__new(TestTask__enter, &first);
    RustFiberTask second_task = RustFiberTask__new(TestTask__other_entry, &second);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &first_task).kind);
    fail_allocation = true;
    HundeStatusResult result = hunde_rust_scheduler_spawn(scheduler, &second_task);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(FIBER_ALLOCATION_FAILED, result.error.code);
    TEST_ASSERT_EQUAL_UINT64(1, hunde_rust_scheduler_live(scheduler));
    fail_allocation = false;
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(1, first.visits);
    TEST_ASSERT_EQUAL_UINT64(0, second.visits);
}

void test_task_completion_resumes_joiner_without_an_external_event(void)
{
    TestTask parent = TestTask__new(scheduler, true);
    TestTask child = TestTask__new(scheduler, false);
    RustFiberTask parent_task = RustFiberTask__new(TestTask__enter, &parent);
    RustFiberTask child_task = RustFiberTask__new(TestTask__complete, &child);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &parent_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &child_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(2, parent.visits);
    TEST_ASSERT_EQUAL_UINT64(1, child.visits);
    TEST_ASSERT_EQUAL_UINT64(0, hunde_rust_scheduler_live(scheduler));
}

void test_fibre_limit_preserves_entries_and_scheduler_can_be_reused(void)
{
    TestTask context = TestTask__new(scheduler, false);
    RustFiberTask task = RustFiberTask__new(TestTask__enter, &context);
    for (size_t index = 0; index < FIBER_SCHEDULER_DEFAULT_MAX_FIBERS; index++)
    {
        TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &task).kind);
    }
    RustFiberTask rejected = RustFiberTask__new(TestTask__other_entry, &context);
    HundeStatusResult result = hunde_rust_scheduler_spawn(scheduler, &rejected);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(FIBER_LIMIT_REACHED, result.error.code);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(FIBER_SCHEDULER_DEFAULT_MAX_FIBERS, context.visits);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(FIBER_SCHEDULER_DEFAULT_MAX_FIBERS + 1, context.visits);
}

void test_native_callback_error_releases_finished_fibre_and_keeps_sibling(void)
{
    TestTask context = TestTask__new(scheduler, false);
    RustFiberTask failed = RustFiberTask__new(TestTask__fail, &context);
    RustFiberTask sibling = RustFiberTask__new(TestTask__enter, &context);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &failed).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &sibling).kind);
    HundeStatusResult result = hunde_rust_scheduler_run(scheduler);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_FALSE(QueueWait__has_adapter());
    TEST_ASSERT_EQUAL(FIBER_INVALID_STATE, result.error.code);
    TEST_ASSERT_EQUAL_UINT64(1, hunde_rust_scheduler_live(scheduler));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(1, context.visits);
}

static HundeStatusResult TestTask__read(HundeHostContext context)
{
    TestTaskPtr self = context;
    DelayedQueue__receive(&delayed_reply);
    self->visits++;
    return HundeStatusResult__success();
}

void test_native_queue_wait_parks_only_its_fibre(void)
{
    TestTask reader = TestTask__new(scheduler, false);
    TestTask sibling = TestTask__new(scheduler, false);
    RustFiberTask reader_task = RustFiberTask__new(TestTask__read, &reader);
    RustFiberTask sibling_task = RustFiberTask__new(TestTask__other_entry, &sibling);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &reader_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &sibling_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(0, reader.visits);
    TEST_ASSERT_EQUAL_UINT64(10, sibling.visits);
    TEST_ASSERT_EQUAL_UINT64(1, hunde_rust_scheduler_waiting(scheduler));
    DelayedQueue__publish(&delayed_reply);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_wake(scheduler).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(1, reader.visits);
}

typedef struct ReleaseTask
{
    BufferDescriptor buffer;
    BufferStatus status;
} ReleaseTask;

typedef ReleaseTask * ReleaseTaskPtr;

static ReleaseTask ReleaseTask__new(BufferID id)
{
    return (ReleaseTask){.buffer = BufferDescriptor__new(id, BUFFER_READ), .status = BUFFER_UNAVAILABLE};
}

static HundeStatusResult ReleaseTask__enter(HundeHostContext context)
{
    ReleaseTaskPtr self = context;
    self->status = BufferClient__release(&self->buffer);
    return HundeStatusResult__success();
}

static void resume_scheduler(void)
{
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_wake(scheduler).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
}

static QueueWaitGate local_gate = QUEUE_WAIT_GATE_INITIALIZER;
static unsigned local_gate_visits;

static HundeStatusResult acquire_local_gate(HundeHostContext context)
{
    match_result(
        QueueWaitGate__acquire(&local_gate),
        success(value)
        {
            local_gate_visits++;
            QueueWaitGate__release(&local_gate);
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

static HundeStatusResult release_local_gate(HundeHostContext context)
{
    TEST_ASSERT_EQUAL_UINT(0, local_gate_visits);
    QueueWaitGate__release(&local_gate);
    return HundeStatusResult__success();
}

void test_gate_release_resumes_parked_contender_without_an_external_event(void)
{
    local_gate_visits = 0;
    match_result(QueueWaitGate__acquire(&local_gate),
        success_do_nothing(),
        error_test_fail()
    );
    RustFiberTask contender = RustFiberTask__new(acquire_local_gate, scheduler);
    RustFiberTask owner = RustFiberTask__new(release_local_gate, scheduler);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &contender).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &owner).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT(1, local_gate_visits);
    TEST_ASSERT_EQUAL_UINT64(0, hunde_rust_scheduler_live(scheduler));
}

static uint64_t local_requested;
static uint64_t local_completed;
static unsigned local_work_count;

static RustServiceCompletion RustServiceCompletion__new_local(HundeAbiResultKind kind, Anything value)
{
    return (RustServiceCompletion){.completion = {.kind = kind, .value = value}};
}

static RustServiceCompletion execute_local(HundeComponentContext context, ConstHundeIORequestV6Ptr request)
{
    TEST_ASSERT_EQUAL_UINT64(HUNDE_LOCAL_IO_SESSION, request->session);
    TEST_ASSERT_EQUAL_UINT64(99, request->pid);
    if (local_completed == request->offset)
    {
        return RustServiceCompletion__new_local(HUNDE_ABI_SUCCESS, request->offset);
    }
    TEST_ASSERT_TRUE(local_requested == local_completed || local_requested == request->offset);
    local_requested = request->offset;
    return RustServiceCompletion__new_local(HUNDE_IO_PENDING, 0);
}

static HundeStatusResult disconnect_local(HundeComponentContext context, uint64_t session)
{
    return HundeStatusResult__success();
}

static RustIOCallbacks RustIOCallbacks__new_local(void)
{
    return (RustIOCallbacks){.execute = execute_local, .disconnect = disconnect_local};
}

static HundeStatusResult TestTask__local_io(HundeHostContext context)
{
    TestTaskPtr self = context;
    IORequest request = IORequest__new(IO_REQUEST_SEEK, IO_FILESYSTEM_HANDLE_BASE);
    request.offset = self->park ? 29 : 17;
    RustIOCallbacks callbacks = RustIOCallbacks__new_local();
    IOReply reply = RustIOCallbacks__execute_local(&callbacks, 99, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(request.offset, reply.value);
    self->visits++;
    return HundeStatusResult__success();
}

static HundeStatusResult complete_local_io(HundeHostContext context)
{
    while (local_work_count < 2)
    {
        if (local_requested != local_completed)
        {
            local_completed = local_requested;
            local_work_count++;
            TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_wake(scheduler).kind);
        }
        if (local_work_count < 2)
        {
            TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_park(scheduler).kind);
        }
    }
    return HundeStatusResult__success();
}

void test_local_io_serializes_fibres_and_completes_without_external_events(void)
{
    local_requested = local_completed = local_work_count = 0;
    TestTask first = TestTask__new(scheduler, false);
    TestTask second = TestTask__new(scheduler, true);
    RustFiberTask first_task = RustFiberTask__new(TestTask__local_io, &first);
    RustFiberTask second_task = RustFiberTask__new(TestTask__local_io, &second);
    RustFiberTask worker = RustFiberTask__new(complete_local_io, scheduler);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &first_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &second_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &worker).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(1, first.visits);
    TEST_ASSERT_EQUAL_UINT64(1, second.visits);
    TEST_ASSERT_EQUAL_UINT(2, local_work_count);
}

void test_buffer_manager_serializes_connection_and_replies_and_recovers_from_failure(void)
{
    ReleaseTask first = ReleaseTask__new(11);
    ReleaseTask second = ReleaseTask__new(22);
    TestTask sibling = TestTask__new(scheduler, false);
    RustFiberTask first_task = RustFiberTask__new(ReleaseTask__enter, &first);
    RustFiberTask second_task = RustFiberTask__new(ReleaseTask__enter, &second);
    RustFiberTask sibling_task = RustFiberTask__new(TestTask__other_entry, &sibling);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &first_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &second_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &sibling_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL_UINT64(10, sibling.visits);
    TEST_ASSERT_EQUAL_UINT(1, manager_opens);
    TEST_ASSERT_EQUAL_UINT(0, manager_submits);
    DelayedQueue__publish(&delayed_open);
    resume_scheduler();
    TEST_ASSERT_EQUAL_UINT(1, manager_submits);
    TEST_ASSERT_EQUAL_UINT64(11, last_request);
    DelayedQueue__publish(&delayed_reply);
    resume_scheduler();
    TEST_ASSERT_EQUAL(BUFFER_OK, first.status);
    TEST_ASSERT_EQUAL(BUFFER_UNAVAILABLE, second.status);
    TEST_ASSERT_EQUAL_UINT(2, manager_submits);
    TEST_ASSERT_EQUAL_UINT64(22, last_request);
    TEST_ASSERT_EQUAL_UINT(1, manager_opens);
    DelayedQueue__publish(&delayed_reply);
    resume_scheduler();
    TEST_ASSERT_EQUAL(BUFFER_OK, second.status);
    TEST_ASSERT_EQUAL_UINT64(0, hunde_rust_scheduler_live(scheduler));

    fail_submit = true;
    first.status = BUFFER_OK;
    second.status = BUFFER_UNAVAILABLE;
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &first_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_spawn(scheduler, &second_task).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_scheduler_run(scheduler).kind);
    TEST_ASSERT_EQUAL(BUFFER_UNAVAILABLE, first.status);
    TEST_ASSERT_EQUAL_UINT(1, manager_closes);
    TEST_ASSERT_EQUAL_UINT(2, manager_opens);
    DelayedQueue__publish(&delayed_open);
    resume_scheduler();
    DelayedQueue__publish(&delayed_reply);
    resume_scheduler();
    TEST_ASSERT_EQUAL(BUFFER_OK, second.status);
    TEST_ASSERT_EQUAL_UINT(0, active_transactions);
}

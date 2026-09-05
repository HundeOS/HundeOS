#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "unity.h"

#include "../../../user/lib/async_io/async_io.h"
#include "../../../user/lib/sys/mem.h"

static void * allocated_stack;
static bool allocated_stack_present;
static uint64_t freed_stack_count = 0;

void * mem_alloc_pages(size_t size)
{
    allocated_stack = malloc(size);
    allocated_stack_present = true;
    return allocated_stack;
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything) malloc(size));
}

Result(Anything) mem_alloc_pages_result(size_t size)
{
    return Success(Anything, (Anything) mem_alloc_pages(size));
}

MemStatus mem_free_pages(void * address)
{
    if (allocated_stack_present && address == allocated_stack)
    {
        freed_stack_count++;
        free(address);
        allocated_stack_present = false;
    }

    return MEM_OK;
}

#include "../../../user/lib/fiber/fiber.c"
#include "../../../user/lib/fiber/scheduler.c"
#include "../../../user/lib/async_io/async_io.c"

typedef struct TestAsyncIOContext
{
    uint64_t request_count;
    uint64_t last_request_id;
}
TestAsyncIOContext;

typedef TestAsyncIOContext * TestAsyncIOContextPtr;

static ServiceEvent wait_events[4];
static size_t wait_event_count;
static size_t wait_event_index;
static ServiceBlockReadResult next_async_block_request_id;

static TestAsyncIOContext TestAsyncIOContext__new(void)
{
    return (TestAsyncIOContext) {
        .request_count = 0,
        .last_request_id = 0,
    };
}

static Result(AsyncIOCode) register_success(Anything service_context)
{
    return Success(AsyncIOCode, ASYNC_IO_OK);
}

static Result(FiberCode) record_request(FiberPtr fiber, Anything service_context, ServiceEvent request)
{
    TestAsyncIOContextPtr context = (TestAsyncIOContextPtr) service_context;
    context->request_count++;
    context->last_request_id = ServiceEvent__request_id(&request);
    return Success(FiberCode, FIBER_OK);
}

static Result(FiberCode) wait_for_response(FiberPtr fiber, Anything service_context, ServiceEvent request)
{
    TestAsyncIOContextPtr context = (TestAsyncIOContextPtr) service_context;
    context->request_count++;

    match_result(AsyncIOPtr, AsyncIO__current(),
        success(async_io)
        {
            match_result(FiberCode, FiberScheduler__await_current(&async_io->scheduler, FiberWaitKey__request(42)),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    match_result(FiberWakeValue, Fiber__take_wake_value(fiber),
        success(value)
        {
            context->last_request_id = value.value;
            return Success(FiberCode, FIBER_OK);
        },
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    return Success(FiberCode, FIBER_OK);
}

void setUp(void)
{
    allocated_stack_present = false;
    freed_stack_count = 0;
}

void tearDown(void)
{
    if (allocated_stack_present)
    {
        free(allocated_stack);
        allocated_stack_present = false;
    }
}

ServiceCode service_wait(ServiceEventPtr event)
{
    if (wait_event_index >= wait_event_count)
    {
        return SERVICE_COMPLETE;
    }

    *event = wait_events[wait_event_index];
    wait_event_index++;
    return SERVICE_OK;
}

ServiceBlockReadResult service_block_read_device_async(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    return next_async_block_request_id;
}

ServiceBlockReadResult service_block_write_device_async(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    return next_async_block_request_id;
}

void test_async_io_start_registers_and_spawns_fiber_for_request(void)
{
    AsyncIO async_io = AsyncIO__new(0, record_request);
    TestAsyncIOContext context = TestAsyncIOContext__new();
    wait_events[0] = ServiceEvent__request(SERVICE_EVENT_FILESYSTEM_IO, 7, 0);
    wait_event_count = 1;
    wait_event_index = 0;

    match_result(AsyncIOCode, AsyncIO__start(&async_io, register_success, (Anything) &context, record_request),
        success(code)
        {
            TEST_ASSERT_EQUAL(ASYNC_IO_WAIT_COMPLETE, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, context.request_count);
    TEST_ASSERT_EQUAL(7, context.last_request_id);
    TEST_ASSERT_EQUAL(0, AsyncIO__live_fiber_count(&async_io));
    TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
}

void test_async_io_new_uses_default_max_fibers(void)
{
    AsyncIO async_io = AsyncIO__new(0, record_request);

    TEST_ASSERT_EQUAL(ASYNC_IO_DEFAULT_MAX_FIBERS, AsyncIO__max_fibers(&async_io));
}

void test_async_io_new_with_max_fibers_passes_limit_to_scheduler(void)
{
    AsyncIO async_io = AsyncIO__new_with_max_fibers(0, record_request, 3);

    TEST_ASSERT_EQUAL(3, AsyncIO__max_fibers(&async_io));
}

void test_async_io_response_without_waiter_is_queued(void)
{
    AsyncIO async_io = AsyncIO__new(0, record_request);
    ServiceEvent response = ServiceEvent__response(SERVICE_EVENT_BLOCK_IO, 9, 123);

    match_result(AsyncIOCode, AsyncIO__deliver_response(&async_io, &response),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, AsyncIO__pending_response_count(&async_io));

    match_result(ServiceEvent, AsyncIO__take_response(&async_io, 9),
        success(queued)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_RESPONSE, ServiceEvent__kind(&queued));
            TEST_ASSERT_EQUAL(SERVICE_EVENT_BLOCK_IO, queued.type);
            TEST_ASSERT_EQUAL(9, ServiceEvent__request_id(&queued));
            TEST_ASSERT_EQUAL(123, queued.data);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(0, AsyncIO__pending_response_count(&async_io));
}

void test_async_io_response_wakes_waiting_fiber(void)
{
    TestAsyncIOContext context = TestAsyncIOContext__new();
    AsyncIO async_io = AsyncIO__new((Anything) &context, wait_for_response);
    ServiceEvent request = ServiceEvent__request(SERVICE_EVENT_FILESYSTEM_IO, 1, 0);
    ServiceEvent response = ServiceEvent__response(SERVICE_EVENT_BLOCK_IO, 42, 0);

    match_result(AsyncIOCode, AsyncIO__spawn_request(&async_io, &request),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FiberCode, FiberScheduler__run_ready_until_blocked(&async_io.scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, context.request_count);
    TEST_ASSERT_EQUAL(1, FiberScheduler__waiting_count(&async_io.scheduler));

    match_result(AsyncIOCode, AsyncIO__deliver_response(&async_io, &response),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, FiberScheduler__ready_count(&async_io.scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__waiting_count(&async_io.scheduler));

    match_result(FiberCode, FiberScheduler__run_ready_until_blocked(&async_io.scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, context.request_count);
    TEST_ASSERT_EQUAL(42, context.last_request_id);
    TEST_ASSERT_EQUAL(0, AsyncIO__live_fiber_count(&async_io));
    TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
}

void test_async_io_spawn_request_rejects_when_live_fiber_limit_reached(void)
{
    TestAsyncIOContext context = TestAsyncIOContext__new();
    AsyncIO async_io = AsyncIO__new_with_max_fibers((Anything) &context, wait_for_response, 1);
    ServiceEvent first = ServiceEvent__request(SERVICE_EVENT_FILESYSTEM_IO, 1, 0);
    ServiceEvent second = ServiceEvent__request(SERVICE_EVENT_FILESYSTEM_IO, 2, 0);

    match_result(AsyncIOCode, AsyncIO__spawn_request(&async_io, &first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FiberCode, FiberScheduler__run_ready_until_blocked(&async_io.scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, AsyncIO__live_fiber_count(&async_io));

    match_result(AsyncIOCode, AsyncIO__spawn_request(&async_io, &second),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(ASYNC_IO_FIBER_FAILED, code);
        }
    );

    ServiceEvent response = ServiceEvent__response(SERVICE_EVENT_BLOCK_IO, 42, 0);
    match_result(AsyncIOCode, AsyncIO__deliver_response(&async_io, &response),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FiberCode, FiberScheduler__run_ready_until_blocked(&async_io.scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(0, AsyncIO__live_fiber_count(&async_io));
    TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
}

void test_io_block_read_takes_queued_async_response(void)
{
    TestAsyncIOContext context = TestAsyncIOContext__new();
    AsyncIO async_io = AsyncIO__new((Anything) &context, record_request);
    current_async_io = Some(AsyncIOPtr, &async_io);
    next_async_block_request_id = 77;

    ServiceEvent response = ServiceEvent__response(SERVICE_EVENT_BLOCK_IO, 77, 1);
    match_result(AsyncIOCode, AsyncIO__deliver_response(&async_io, &response),
        success_do_nothing(),
        error_test_fail()
    );

    uint8_t buffer[512] = { 0 };
    match_result(ServiceBlockReadResult, IO__block_read(&bstr("/dev/test"), 4, 1, (Address) buffer, sizeof(buffer)),
        success(blocks)
        {
            TEST_ASSERT_EQUAL(1, blocks);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(0, AsyncIO__pending_response_count(&async_io));
}

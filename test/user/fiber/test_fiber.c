#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "unity.h"

#include "../../../user/lib/fiber/scheduler.h"
#include "../../../user/lib/sys/mem.h"

static size_t allocated_stack_size = 0;
static void * allocated_stack;
static bool allocated_stack_present;
static uint64_t freed_stack_count = 0;

void * mem_alloc_pages(size_t size)
{
    allocated_stack_size = size;
    allocated_stack = malloc(size);
    allocated_stack_present = true;
    return allocated_stack;
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

Result(Anything) mem_alloc_pages_result(size_t size)
{
    return Success(Anything, (Anything)mem_alloc_pages(size));
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

#include "../../../user/lib/channel/signal.c"
#include "../../../user/lib/fiber/fiber.c"
#include "../../../user/lib/fiber/scheduler.c"

typedef struct TestFiberContext
{
    FiberSchedulerPtr scheduler;
    ChannelSignalPtr channel;
    uint64_t run_count;
    uint64_t wake_value;
} TestFiberContext;

typedef TestFiberContext * TestFiberContextPtr;

static TestFiberContext TestFiberContext__new(FiberSchedulerPtr scheduler)
{
    return (TestFiberContext){
        .scheduler = scheduler,
        .channel = 0,
        .run_count = 0,
        .wake_value = 0,
    };
}

void setUp(void)
{
    ChannelRequestContext__swap(&None(RequestTicket));
    allocated_stack_size = 0;
    allocated_stack_present = false;
    freed_stack_count = 0;
}

static Result(FiberCode) ticket_entry(FiberPtr self, Anything argument)
{
    RequestTicket ticket = RequestTicket__new(argument, 1, 42, &None(RequestTicket));
    Optional(RequestTicket) previous = ChannelRequestContext__swap(&Some(RequestTicket, ticket));
    match_result(
        Fiber__yield_to_scheduler(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );
    match_optional(
        ChannelRequestContext__current(),
        some(current)
        {
            TEST_ASSERT_TRUE(RequestTicket__equals(&ticket, &current));
        },
        none_test_fail()
    );
    ChannelRequestContext__swap(&previous);
    return Success(FiberCode, FIBER_OK);
}

void test_request_context_is_isolated_across_fiber_switches(void)
{
    FiberContext context;
    FiberPtr first = unwrap_result(Fiber__heap_new_with_stack_size(ticket_entry, 101, 65536), (FiberPtr){});
    FiberPtr second = unwrap_result(Fiber__heap_new_with_stack_size(ticket_entry, 202, 65536), (FiberPtr){});
    match_result(Fiber__resume(first, &context),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(ChannelRequestContext__current(),
        some_test_fail(),
        none_do_nothing()
    );
    match_result(Fiber__resume(second, &context),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(ChannelRequestContext__current(),
        some_test_fail(),
        none_do_nothing()
    );
    match_result(Fiber__mark_ready(first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Fiber__resume(first, &context),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Fiber__mark_ready(second),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Fiber__resume(second, &context),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(ChannelRequestContext__current(),
        some_test_fail(),
        none_do_nothing()
    );
    // The fixture tracks only the most recent stack allocation.
    free((void *)Fiber__stack_base(first));
    Fiber__dispose(first);
    Fiber__dispose(second);
}

void tearDown(void)
{
    if (allocated_stack_present)
    {
        free(allocated_stack);
        allocated_stack_present = false;
    }
}

static Result(FiberCode) waiting_entry(FiberPtr self, Anything argument)
{
    TestFiberContextPtr context = (TestFiberContextPtr)argument;
    context->run_count++;

    match_result(
        FiberScheduler__await_current(context->scheduler, FiberWaitKey__block_request(42)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    match_result(
        Fiber__take_wake_value(self),
        success(value)
        {
            context->wake_value = value.value;
            return Success(FiberCode, FIBER_OK);
        },
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    return Success(FiberCode, FIBER_OK);
}

static Result(FiberCode) immediate_entry(FiberPtr self, Anything argument)
{
    TestFiberContextPtr context = (TestFiberContextPtr)argument;
    context->run_count++;
    return Success(FiberCode, FIBER_OK);
}

static Result(FiberCode) channel_waiting_entry(FiberPtr self, Anything argument)
{
    TestFiberContextPtr context = (TestFiberContextPtr)argument;
    context->run_count++;

    match_result(
        FiberScheduler__await_channel_signal(context->scheduler, context->channel),
        success_do_nothing(),
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    match_result(
        Fiber__take_wake_value(self),
        success(value)
        {
            context->wake_value = value.value;
            return Success(FiberCode, FIBER_OK);
        },
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    return Success(FiberCode, FIBER_OK);
}

void test_fiber_heap_new_allocates_default_stack(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);

    match_result(
        Fiber__heap_new(immediate_entry, (Anything)&context),
        success(fiber)
        {
            TEST_ASSERT_EQUAL(FIBER_DEFAULT_STACK_SIZE, allocated_stack_size);
            TEST_ASSERT_EQUAL((Address)allocated_stack, Fiber__stack_base(fiber));
            TEST_ASSERT_EQUAL(FIBER_DEFAULT_STACK_SIZE, Fiber__stack_size(fiber));
            TEST_ASSERT_EQUAL(0, Fiber__stack_top(fiber) % FIBER_STACK_ALIGNMENT);

            Fiber__dispose(fiber);
            TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
        },
        error_test_fail()
    );
}

void test_fiber_heap_new_with_stack_size_allocates_requested_stack(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);
    size_t stack_size = 1024 * 1024 * 2;

    match_result(
        Fiber__heap_new_with_stack_size(immediate_entry, (Anything)&context, stack_size),
        success(fiber)
        {
            TEST_ASSERT_EQUAL(stack_size, allocated_stack_size);
            TEST_ASSERT_EQUAL(stack_size, Fiber__stack_size(fiber));

            Fiber__dispose(fiber);
            TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
        },
        error_test_fail()
    );
}

void test_fiber_scheduler_spawn_queues_ready_fiber(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);

    match_result(
        FiberScheduler__spawn(&scheduler, immediate_entry, (Anything)&context),
        success(fiber)
        {
            TEST_ASSERT_EQUAL(FIBER_READY, Fiber__state(fiber));
            TEST_ASSERT_EQUAL(1, FiberScheduler__ready_count(&scheduler));
            TEST_ASSERT_EQUAL(1, FiberScheduler__live_count(&scheduler));
        },
        error_test_fail()
    );

    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_fiber_scheduler_run_ready_finishes_immediate_fiber(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);

    match_result(FiberScheduler__spawn(&scheduler, immediate_entry, (Anything)&context),
        success_do_nothing(),
                 error_test_fail()
    );

    match_result(
        FiberScheduler__run_ready(&scheduler),
        success(code)
        {
            TEST_ASSERT_EQUAL(FIBER_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, context.run_count);
    TEST_ASSERT_EQUAL(0, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__waiting_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__live_count(&scheduler));
    TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
}

void test_fiber_scheduler_await_parks_current_fiber_on_wait_key(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);
    FiberPtr fiber;

    match_result(
        FiberScheduler__spawn(&scheduler, waiting_entry, (Anything)&context),
        success(spawned)
        {
            fiber = spawned;
        },
        error_test_fail()
    );

    match_result(
        FiberScheduler__run_ready(&scheduler),
        success(code)
        {
            TEST_ASSERT_EQUAL(FIBER_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, context.run_count);
    TEST_ASSERT_EQUAL(FIBER_WAITING, Fiber__state(fiber));
    TEST_ASSERT_EQUAL(0, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(1, FiberScheduler__waiting_count(&scheduler));
    TEST_ASSERT_EQUAL(1, FiberScheduler__live_count(&scheduler));

    match_optional(
        Fiber__wait_key(fiber),
        some(key)
        {
            TEST_ASSERT_EQUAL(FIBER_WAIT_BLOCK_REQUEST, key.kind);
            TEST_ASSERT_EQUAL(42, key.id);
        },
        none_test_fail()
    );

    Fiber__dispose(fiber);
}

void test_fiber_scheduler_wake_moves_waiting_fiber_back_to_ready(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);
    FiberPtr fiber = 0;
    FiberWaitKey key = FiberWaitKey__block_request(42);

    match_result(
        FiberScheduler__spawn(&scheduler, waiting_entry, (Anything)&context),
        success(spawned)
        {
            fiber = spawned;
        },
        error_test_fail()
    );
    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, 99);
    match_result(
        FiberScheduler__wake(&scheduler, key, value),
        success(code)
        {
            TEST_ASSERT_EQUAL(FIBER_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(FIBER_READY, Fiber__state(fiber));
    TEST_ASSERT_EQUAL(1, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__waiting_count(&scheduler));

    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, context.run_count);
    TEST_ASSERT_EQUAL(99, context.wake_value);
    TEST_ASSERT_EQUAL(0, FiberScheduler__live_count(&scheduler));
    TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
}

void test_fiber_scheduler_spawn_rejects_when_live_limit_reached(void)
{
    FiberScheduler scheduler = FiberScheduler__new_with_max_fibers(1);
    TestFiberContext context = TestFiberContext__new(&scheduler);

    match_result(FiberScheduler__spawn(&scheduler, waiting_entry, (Anything)&context),
        success_do_nothing(),
                 error_test_fail()
    );

    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, FiberScheduler__live_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(1, FiberScheduler__waiting_count(&scheduler));

    match_result(
        FiberScheduler__spawn(&scheduler, immediate_entry, (Anything)&context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(FIBER_LIMIT_REACHED, code);
        }
    );

    FiberWaitKey key = FiberWaitKey__block_request(42);
    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, 77);
    match_result(FiberScheduler__wake(&scheduler, key, value),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(0, FiberScheduler__live_count(&scheduler));
    TEST_ASSERT_EQUAL_UINT64(1, freed_stack_count);
}

void test_fiber_scheduler_wake_rejects_unknown_wait_key(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    FiberWaitKey key = FiberWaitKey__block_request(123);
    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, 1);

    match_result(
        FiberScheduler__wake(&scheduler, key, value),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(FIBER_WAIT_NOT_FOUND, code);
        }
    );
}

void test_fiber_scheduler_await_channel_signal_parks_on_channel_state(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    ChannelSignalState state = ChannelSignalState__new();
    ChannelSignal channel = ChannelSignal__new(&state);
    TestFiberContext context = TestFiberContext__new(&scheduler);
    context.channel = &channel;
    FiberPtr fiber = 0;

    match_result(
        FiberScheduler__spawn(&scheduler, channel_waiting_entry, (Anything)&context),
        success(spawned)
        {
            fiber = spawned;
        },
        error_test_fail()
    );

    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(FIBER_WAITING, Fiber__state(fiber));
    match_optional(
        Fiber__wait_key(fiber),
        some(key)
        {
            TEST_ASSERT_EQUAL(FIBER_WAIT_CHANNEL, key.kind);
            TEST_ASSERT_EQUAL_UINT64((uint64_t)&state, key.id);
        },
        none_test_fail()
    );

    Fiber__dispose(fiber);
}

void test_fiber_scheduler_wake_channel_signal_moves_waiter_to_ready(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    ChannelSignalState state = ChannelSignalState__new();
    ChannelSignal channel = ChannelSignal__new(&state);
    TestFiberContext context = TestFiberContext__new(&scheduler);
    context.channel = &channel;

    match_result(FiberScheduler__spawn(&scheduler, channel_waiting_entry, (Anything)&context),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    ChannelSignal__notify_data(&channel);
    match_result(FiberScheduler__wake_channel_signal(&scheduler, &channel),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__waiting_count(&scheduler));

    match_result(FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(ChannelSignal__data_epoch(&channel), context.wake_value);
    TEST_ASSERT_EQUAL(0, FiberScheduler__live_count(&scheduler));
}

void test_fiber_invalid_state_error_includes_current_state(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);
    FiberPtr fiber = 0;

    match_result(
        Fiber__heap_new(immediate_entry, (Anything)&context),
        success(created)
        {
            fiber = created;
        },
        error_test_fail()
    );

    match_result(
        Fiber__resume(fiber, &scheduler.context),
        success(code)
        {
            TEST_ASSERT_EQUAL(FIBER_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Fiber__mark_ready(fiber),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(FIBER_INVALID_STATE, code);
            TEST_ASSERT_EQUAL_BSTRING(bstr("Invalid fiber state 5"), message);
        }
    );

    Fiber__dispose(fiber);
}

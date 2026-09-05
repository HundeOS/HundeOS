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

typedef struct TestFiberContext
{
    FiberSchedulerPtr scheduler;
    uint64_t run_count;
    uint64_t wake_value;
}
TestFiberContext;

typedef TestFiberContext * TestFiberContextPtr;

static TestFiberContext TestFiberContext__new(FiberSchedulerPtr scheduler)
{
    return (TestFiberContext) {
        .scheduler = scheduler,
        .run_count = 0,
        .wake_value = 0,
    };
}

void setUp(void)
{
    allocated_stack_size = 0;
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

static Result(FiberCode) waiting_entry(FiberPtr self, Anything argument)
{
    TestFiberContextPtr context = (TestFiberContextPtr) argument;
    context->run_count++;

    match_result(FiberCode, FiberScheduler__await_current(context->scheduler, FiberWaitKey__block_request(42)),
        success_do_nothing(),
        error(code, message)
        {
            return Error(FiberCode, message, code);
        }
    );

    match_result(FiberWakeValue, Fiber__take_wake_value(self),
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
    TestFiberContextPtr context = (TestFiberContextPtr) argument;
    context->run_count++;
    return Success(FiberCode, FIBER_OK);
}

void test_fiber_heap_new_allocates_default_stack(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);

    match_result(FiberPtr, Fiber__heap_new(immediate_entry, (Anything) &context),
        success(fiber)
        {
            TEST_ASSERT_EQUAL(FIBER_DEFAULT_STACK_SIZE, allocated_stack_size);
            TEST_ASSERT_EQUAL((Address) allocated_stack, Fiber__stack_base(fiber));
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

    match_result(FiberPtr, Fiber__heap_new_with_stack_size(immediate_entry, (Anything) &context, stack_size),
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

    match_result(FiberPtr, FiberScheduler__spawn(&scheduler, immediate_entry, (Anything) &context),
        success(fiber)
        {
            TEST_ASSERT_EQUAL(FIBER_READY, Fiber__state(fiber));
            TEST_ASSERT_EQUAL(1, FiberScheduler__ready_count(&scheduler));
            TEST_ASSERT_EQUAL(1, FiberScheduler__live_count(&scheduler));
        },
        error_test_fail()
    );

    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_fiber_scheduler_run_ready_finishes_immediate_fiber(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);

    match_result(FiberPtr, FiberScheduler__spawn(&scheduler, immediate_entry, (Anything) &context),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
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

    match_result(FiberPtr, FiberScheduler__spawn(&scheduler, waiting_entry, (Anything) &context),
        success(spawned)
        {
            fiber = spawned;
        },
        error_test_fail()
    );

    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
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

    match_optional(FiberWaitKey, Fiber__wait_key(fiber),
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

    match_result(FiberPtr, FiberScheduler__spawn(&scheduler, waiting_entry, (Anything) &context),
        success(spawned)
        {
            fiber = spawned;
        },
        error_test_fail()
    );
    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, 99);
    match_result(FiberCode, FiberScheduler__wake(&scheduler, key, value),
        success(code)
        {
            TEST_ASSERT_EQUAL(FIBER_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(FIBER_READY, Fiber__state(fiber));
    TEST_ASSERT_EQUAL(1, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__waiting_count(&scheduler));

    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
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

    match_result(FiberPtr, FiberScheduler__spawn(&scheduler, waiting_entry, (Anything) &context),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, FiberScheduler__live_count(&scheduler));
    TEST_ASSERT_EQUAL(0, FiberScheduler__ready_count(&scheduler));
    TEST_ASSERT_EQUAL(1, FiberScheduler__waiting_count(&scheduler));

    match_result(FiberPtr, FiberScheduler__spawn(&scheduler, immediate_entry, (Anything) &context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(FIBER_LIMIT_REACHED, code);
        }
    );

    FiberWaitKey key = FiberWaitKey__block_request(42);
    FiberWakeValue value = FiberWakeValue__new(&key, FIBER_OK, 77);
    match_result(FiberCode, FiberScheduler__wake(&scheduler, key, value),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FiberCode, FiberScheduler__run_ready(&scheduler),
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

    match_result(FiberCode, FiberScheduler__wake(&scheduler, key, value),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(FIBER_WAIT_NOT_FOUND, code);
        }
    );
}

void test_fiber_invalid_state_error_includes_current_state(void)
{
    FiberScheduler scheduler = FiberScheduler__new();
    TestFiberContext context = TestFiberContext__new(&scheduler);
    FiberPtr fiber = 0;

    match_result(FiberPtr, Fiber__heap_new(immediate_entry, (Anything) &context),
        success(created)
        {
            fiber = created;
        },
        error_test_fail()
    );

    match_result(FiberCode, Fiber__resume(fiber, &scheduler.context),
        success(code)
        {
            TEST_ASSERT_EQUAL(FIBER_OK, code);
        },
        error_test_fail()
    );

    match_result(FiberCode, Fiber__mark_ready(fiber),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(FIBER_INVALID_STATE, code);
            TEST_ASSERT_EQUAL_STRING("Invalid fiber state 5", BString__cstr(&message));
        }
    );

    Fiber__dispose(fiber);
}

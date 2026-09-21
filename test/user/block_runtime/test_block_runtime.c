#include <stdlib.h>
#include "unity.h"
#include "../../../user/lib/sys/mem.h"
#include "../../../user/lib/channel/block_runtime.c"
#include "../../../user/lib/channel/signal.c"
#include "../../../user/lib/fiber/fiber.c"
#include "../../../user/lib/fiber/scheduler.c"

static void * stack_allocation;
static void * fiber_allocation;
static bool fail_allocation;
static unsigned event_polls;
static unsigned work_polls;
static unsigned idle_waits;

void setUp(void)
{
    stack_allocation = NULL;
    fiber_allocation = NULL;
    fail_allocation = false;
    event_polls = 0;
    work_polls = 0;
    idle_waits = 0;
}
void tearDown(void)
{
    free(stack_allocation);
    free(fiber_allocation);
}
Result(Anything) mem_alloc_bytes(size_t size)
{
    if (fail_allocation)
    {
        return Error(Anything, bstr("allocation failed"), MEM_ALLOCATION_FAILED);
    }
    fiber_allocation = malloc(size);
    return Success(Anything, (Anything)fiber_allocation);
}
Result(Anything) mem_alloc_pages_result(size_t size)
{
    stack_allocation = malloc(size);
    return Success(Anything, (Anything)stack_allocation);
}
MemStatus mem_free_pages(void * address)
{
    free(address);
    stack_allocation = NULL;
    return MEM_OK;
}
void Channel__prepare_wait(void)
{
}
void Channel__wait_event(void)
{
    idle_waits++;
}
ServiceCode service_poll(ServiceEventPtr event)
{
    return ++event_polls <= 2 ? SERVICE_EVENT_QUEUE_EMPTY : SERVICE_COMPLETE;
}
bool BlockServer__poll(BlockServerPtr self)
{
    work_polls++;
    return work_polls == 2;
}
void test_block_runtime_runs_worker_then_resumes_it_after_idle(void)
{
    BlockServer server = {0};
    TEST_ASSERT_EQUAL(0, BlockServer__run(&server));
    TEST_ASSERT_EQUAL_UINT(3, work_polls);
    TEST_ASSERT_EQUAL_UINT(2, idle_waits);
}
void test_block_runtime_reports_fibre_allocation_failure(void)
{
    fail_allocation = true;
    BlockServer server = {0};
    TEST_ASSERT_EQUAL(1, BlockServer__run(&server));
    TEST_ASSERT_EQUAL_UINT(0, work_polls);
}

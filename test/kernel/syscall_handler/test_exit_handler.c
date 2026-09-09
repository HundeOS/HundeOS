#include "unity.h"
#include "../../../kernel/syscall/exit_handler.h"
#include "../fixtures/process.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/process.h"
#include "../../../kernel/arch/process.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../fixtures/kernel.h"
#include "../stubs/panic.h"
#include "../stubs/process_context_layout.h"

#define Exit__handler(kernel, proc, ...) Exit__handler((kernel), &(proc), __VA_ARGS__)

static ProcessPtr process = NULL;
static KernelPtr kernel = NULL;
static CPUID cpu = 11;
static struct ProcessContext saved_context;
static ProcessContextPtr context = &saved_context;
static ProcessPtr idle = NULL;
static bool cont_called = false;
static KernelContext parent_context = EMPTY_KERNEL_CONTEXT;

Result(Anything) continuation(ParkedOperationPtr operation)
{
    cont_called = true;

    return Success(Anything, 100);
}

void setUp(void)
{
    saved_context.elr = 0x12345;
    kernel = init_test_kernel_without_filesystem();
    Scheduler__start(kernel->scheduler, cpu);

    Optional(ProcessPtr) idle_opt = Scheduler__idle_process(kernel->scheduler, cpu);
    idle = unwrap_optional(idle_opt, (ProcessPtr){});

    cont_called = false;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

ProcessPtr setup_test_process(void)
{
    process = dummy_process(100, cpu, PROC_STATE_RUNNING);

    Scheduler__schedule(kernel->scheduler, process);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    return process;
}

ProcessPtr setup_test_process_waiting_on_child_process(ProcessPtr parent)
{
    ProcessPtr child = dummy_process_with_parent(100, cpu, PROC_STATE_READY, parent);
    Scheduler__schedule(kernel->scheduler, process);

    parent_context = NEW_KERNEL_CONTEXT(kernel->scheduler, Some(ProcessID, parent->pid));
    Scheduler__park(kernel->scheduler, parent, &parent_context, &Some(Continuation, continuation), PARK_WAIT_CHILD);
    Scheduler__schedule(kernel->scheduler, child);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    return child;
}

void test_exit_handler_interrupts_and_removes_process_from_scheduler(void)
{
    setup_test_process();
    ProcessPtr exited_process = process;
    Optional(ProcessPtr) current_opt = Scheduler__current_process(kernel->scheduler, cpu);

    match_optional(current_opt,
        some(current) { TEST_ASSERT_EQUAL(process, current); },
        none_test_fail()
    );

    Exit__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0);

    match_optional(
        Scheduler__current_process(kernel->scheduler, cpu),
        some(current) { TEST_ASSERT_EQUAL(idle, current); },
        none_test_fail()
    );

    ProcessTablePtr processes = Scheduler__all_processes(kernel->scheduler, cpu);

    match_optional(ProcessTable__find_item(processes, exited_process),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_exit_handler_interrupts_and_removes_process_when_parent_is_waiting_on_process(void)
{
    ProcessPtr parent = setup_test_process();
    ProcessPtr child = setup_test_process_waiting_on_child_process(parent);
    ProcessPtr exited_child = child;
    Optional(ProcessPtr) current_opt = Scheduler__current_process(kernel->scheduler, cpu);

    match_optional(current_opt,
        some(current) { TEST_ASSERT_EQUAL(child, current); },
        none_test_fail()
    );

    Exit__handler(kernel, Some(ProcessPtr, child), 0, 0, 0, 0, 0);
    Scheduler__preempt(kernel->scheduler, cpu,
                       context); // make sure the parent is woken up ( it was waiting on the child

    match_optional(
        Scheduler__current_process(kernel->scheduler, cpu),
        some(current)
        {
            TEST_ASSERT_NOT_EQUAL(idle, current);
            TEST_ASSERT_EQUAL(parent, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
        },
        none_test_fail()
    );

    ProcessTablePtr processes = Scheduler__all_processes(kernel->scheduler, cpu);

    match_optional(ProcessTable__find_item(processes, exited_child),
        some_test_fail(),
        none_do_nothing()
    );

    TEST_ASSERT_TRUE(cont_called);
}

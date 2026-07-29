#include "unity.h"
#include "../../../kernel/syscall/exit_handler.h"
#include "../fixtures/process.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/process.h"
#include "../../../kernel/arch/process.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../fixtures/kernel.h"
#include "../stubs/panic.h"


static ProcessPtr process = NULL;
static KernelPtr kernel = NULL;
static CPUID cpu = 11;
static ProcessContextPtr context = (ProcessContextPtr) 0x12345;
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
    kernel = init_test_kernel_without_filesystem();
    Scheduler__start(kernel->scheduler, cpu);

    Optional(ProcessPtr) idle_opt = Scheduler__idle_process(kernel->scheduler, cpu);
    idle = Unwrap(ProcessPtr, idle_opt, (ProcessPtr){});

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
    Scheduler__park(kernel->scheduler, parent, &parent_context, Some(Continuation, continuation), PARK_WAIT_CHILD);
    Scheduler__schedule(kernel->scheduler, child);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    return child;
}

void test_exit_handler_interrupts_and_removes_process_from_scheduler(void)
{
    setup_test_process();
    Optional(ProcessPtr) current_opt = Scheduler__current_process(kernel->scheduler, cpu);

    Match(ProcessPtr, current_opt)
        IsNone(TEST_FAIL())
        IsSome(current, TEST_ASSERT_EQUAL(process, current));
    End

    Exit__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0);

    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, process->state);

    Match(ProcessPtr, Scheduler__current_process(kernel->scheduler, cpu))
        IsNone(TEST_FAIL());
        IsSome(current, TEST_ASSERT_EQUAL(idle, current));
    End

    ProcessTablePtr processes = Scheduler__all_processes(kernel->scheduler, cpu);

    Match(ArrayIndex, ProcessTable__find_item(processes, process))
        IsSome(val, TEST_FAIL());
    End
}

void test_exit_handler_interrupts_and_removes_process_when_parent_is_waiting_on_process(void)
{
    ProcessPtr parent = setup_test_process();
    ProcessPtr child = setup_test_process_waiting_on_child_process(parent);
    Optional(ProcessPtr) current_opt = Scheduler__current_process(kernel->scheduler, cpu);

    Match(ProcessPtr, current_opt)
        IsNone(TEST_FAIL())
        IsSome(current, TEST_ASSERT_EQUAL(child, current));
    End

    Exit__handler(kernel, Some(ProcessPtr, child), 0, 0, 0, 0, 0);
    Scheduler__preempt(kernel->scheduler, cpu, context); // make sure the parent is woken up ( it was waiting on the child
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, child->state);

    Match(ProcessPtr, Scheduler__current_process(kernel->scheduler, cpu))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_NOT_EQUAL(idle, current);
            TEST_ASSERT_EQUAL(parent, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
        });
    End

    ProcessTablePtr processes = Scheduler__all_processes(kernel->scheduler, cpu);

    Match(ArrayIndex, ProcessTable__find_item(processes, child))
        IsSome(val, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(cont_called);
}

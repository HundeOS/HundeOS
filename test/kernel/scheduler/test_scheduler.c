#include "unity.h"
#include "../stubs/panic.h"
#include "../stubs/cpu.h"
#include "../stubs/process.h"
#include "../fixtures/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/bin/embedded_binaries.h"

static SchedulerPtr scheduler;
static void * continuation_context;
static const CPUID cpu_id = 12;

void setUp(void)
{
    continuation_context = NULL;
    clear_panic_state();
    set_cpu_id(cpu_id);
    current_process_reset();

    scheduler = Scheduler__heap_new();
    Elf idle = Unwrap(Elf, EmbeddedElf__load(kstring("idle")), (Elf){});
    Scheduler__init(scheduler, idle);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
    scheduler = NULL;
}

Result(Anything) continuation(void * context)
{
    continuation_context = context;
    return Success(Anything, 100);
}

void test_scheduler_interrupt_when_no_processes_are_running_stays_in_idle(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref
    Scheduler__start(scheduler, cpu_id);

    Scheduler__preempt(scheduler, cpu_id, context);
    ProcessPtr idle = UnwrapPanic(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id));
    ProcessPtr current = UnwrapPanic(ProcessPtr, Scheduler__current_process(scheduler, cpu_id));

    TEST_ASSERT_EQUAL(idle, current);
    TEST_ASSERT_EQUAL(idle->context, context);
    TEST_ASSERT_EQUAL(idle->state, PROC_STATE_RUNNING);
    TEST_ASSERT_EQUAL(current_running_process(), idle);
}

void test_all_processes_when_processes_are_scheduled()
{
    ProcessPtr proc1 = dummy_process(1, 1, PROC_STATE_READY);
    ProcessPtr proc2 = dummy_process(2, 1, PROC_STATE_READY);
    ProcessPtr proc3 = dummy_process(3, 1, PROC_STATE_READY);

    Scheduler__start(scheduler, 1);
    Scheduler__schedule(scheduler, proc1);
    Scheduler__schedule(scheduler, proc2);
    Scheduler__schedule(scheduler, proc3);

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, 1);

    TEST_ASSERT_EQUAL(3, ProcessTable__len(processes));
    Match(ArrayIndex, ProcessTable__find_item(processes, proc1))
        IsNone(TEST_FAIL())
    End
    Match(ArrayIndex, ProcessTable__find_item(processes, proc2))
        IsNone(TEST_FAIL())
    End
    Match(ArrayIndex, ProcessTable__find_item(processes, proc3))
        IsNone(TEST_FAIL())
    End

}

void test_process_count_when_processes_are_scheduled_across_cpus()
{
    ProcessPtr proc1 = dummy_process(1, 1, PROC_STATE_READY);
    ProcessPtr proc2 = dummy_process(2, 2, PROC_STATE_READY);
    ProcessPtr proc3 = dummy_process(3, 3, PROC_STATE_READY);
    ProcessPtr proc4 = dummy_process(1, 1, PROC_STATE_READY);
    ProcessPtr proc5 = dummy_process(2, 2, PROC_STATE_READY);
    ProcessPtr proc6 = dummy_process(3, 3, PROC_STATE_READY);

    Scheduler__start(scheduler, 1);
    Scheduler__schedule(scheduler, proc1);
    Scheduler__schedule(scheduler, proc2);
    Scheduler__schedule(scheduler, proc3);
    Scheduler__schedule(scheduler, proc4);
    Scheduler__schedule(scheduler, proc5);
    Scheduler__schedule(scheduler, proc6);

    uint64_t count = Scheduler__process_count(scheduler);

    TEST_ASSERT_EQUAL(6, count);
}

void test_all_processes_when_processes_are_scheduled_on_different_cpu()
{
    ProcessPtr proc1 = dummy_process(1, 1, PROC_STATE_READY);
    ProcessPtr proc2 = dummy_process(2, 1, PROC_STATE_READY);
    ProcessPtr proc3 = dummy_process(3, 1, PROC_STATE_READY);

    Scheduler__start(scheduler, 1);
    Scheduler__schedule(scheduler, proc1);
    Scheduler__schedule(scheduler, proc2);
    Scheduler__schedule(scheduler, proc3);

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, 2);

    TEST_ASSERT_EQUAL(0, ProcessTable__len(processes));
}

void test_scheduler_save_context_to_current_process(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__start(scheduler, cpu_id);

    Match(ProcessPtr, Scheduler__save_context(scheduler, cpu_id, context))
        IsNone(TEST_FAIL());
        IsSome(proc,
        {
            TEST_ASSERT_EQUAL(context, proc->context);

            Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
                IsNone(TEST_FAIL());
                IsSome(current, TEST_ASSERT_EQUAL(context, current->context));
            End
        })
    End
}

void test_scheduler_exit_removes_process_when_its_current(void)
{
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr idle = UnwrapPanic(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id));

    ResultMatch(ProcessPtr, Scheduler__exit(scheduler, proc))
        IsError(code, message, TEST_FAIL());
        IsSuccess(exited,
        {
            TEST_ASSERT_EQUAL(proc, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(idle, current);
        })
    End

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(0, ProcessTable__len(processes));
}

void test_scheduler_exit_removes_process_when_its_not_current(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr other_proc = dummy_process(0, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__schedule(scheduler, proc);
    Scheduler__schedule(scheduler, other_proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, other_proc);
        })
    End


    ResultMatch(ProcessPtr, Scheduler__exit(scheduler, proc))
        IsError(code, message, TEST_FAIL());
        IsSuccess(exited,
        {
            TEST_ASSERT_EQUAL(proc, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(other_proc, current);
        })
    End

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(1, ProcessTable__len(processes));
}

void test_scheduler_park_switches_process_to_waiting_and_switches_context(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = (KernelContext) {
        .scheduler = scheduler,
        .proc = Some(ProcessPtr, proc),
    };

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr idle = UnwrapPanic(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id));

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, proc);
        })
    End


    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation)))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(idle, current);
        })
    End

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(1, ProcessTable__len(processes));
}

void test_scheduler_unpark_switches_process_back_to_ready(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = (KernelContext) {
        .scheduler = scheduler,
        .proc = Some(ProcessPtr, proc),
    };

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, proc);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation)))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__unpark(scheduler, proc))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->state);
        })
    End
}

void test_scheduler_unpark_when_not_idle(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr running_proc = dummy_process(0, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = (KernelContext) {
        .proc = Some(ProcessPtr, proc),
        .scheduler = scheduler,
    };

    Scheduler__schedule(scheduler, proc);
    Scheduler__schedule(scheduler, running_proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, running_proc);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation)))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__unpark(scheduler, proc))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->state);
        })
    End

    // it should cause a context switch as CPU is currently idle
    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(running_proc, current);
        })
    End
}

void test_scheduler_park_switches_process_to_waiting_not_switching_context_when_not_current(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr running = dummy_process(0, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = (KernelContext) {
        .proc = Some(ProcessPtr, proc),
        .scheduler = scheduler,
    };

    Scheduler__schedule(scheduler, proc);
    Scheduler__schedule(scheduler, running);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, running);
        })
    End


    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation)))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(running, current);
        })
    End

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(2, ProcessTable__len(processes));
}

void test_scheduler_preempt_when_idle_with_a_process_ready(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref
    CPUID scheduled_cpu = 3;

    ProcessPtr dummy = dummy_process(0, scheduled_cpu, PROC_STATE_READY);

    ResultMatch(CPUID, Scheduler__schedule(scheduler, dummy))
        IsSuccess(cpu, TEST_ASSERT_EQUAL(scheduled_cpu, cpu))
        IsError(code, message, TEST_FAIL());
    End
    ProcessPtr idle = UnwrapPanic(ProcessPtr, Scheduler__idle_process(scheduler, scheduled_cpu));

    Scheduler__start(scheduler, scheduled_cpu);
    // should start with idle
    TEST_ASSERT_EQUAL(idle, current_running_process());
    TEST_ASSERT_EQUAL(idle->state, PROC_STATE_RUNNING);

    Scheduler__preempt(scheduler, scheduled_cpu, context);
    TEST_ASSERT_EQUAL(dummy, current_running_process());
    TEST_ASSERT_EQUAL(0, dummy->last_tick);
    TEST_ASSERT_EQUAL(dummy->state, PROC_STATE_RUNNING);

    TEST_ASSERT_EQUAL(idle->context, context); // check the context was saved
    TEST_ASSERT_EQUAL(idle->state, PROC_STATE_INTERRUPTED); // check the context was saved
    TEST_ASSERT_EQUAL(1, idle->last_tick); // check the context was saved

    Match(ProcessPtr, Scheduler__current_process(scheduler, scheduled_cpu))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(dummy, current);
        });
    End
}

void test_scheduler_preempt_when_idle_with_multiple_processes_ready_selects_longest_waiting(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref

    CPUID dummy_cpu = 11;
    ProcessPtr proc_most_recently_run = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    ProcessPtr proc_2nd_most_recently_run = dummy_process(50, dummy_cpu, PROC_STATE_READY);
    ProcessPtr proc_least_recently_run = dummy_process(0, dummy_cpu, PROC_STATE_READY);

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule(scheduler, proc_most_recently_run);
    Scheduler__schedule(scheduler, proc_2nd_most_recently_run);
    Scheduler__schedule(scheduler, proc_least_recently_run);

    Scheduler__preempt(scheduler, dummy_cpu, context);
    TEST_ASSERT_EQUAL(proc_least_recently_run, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc_least_recently_run->state);

    Match(ProcessPtr, Scheduler__current_process(scheduler, dummy_cpu))
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(proc_least_recently_run, current);
        });
    End
}

void test_scheduler_interrupt_switch_from_one_ready_process_to_another(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref

    CPUID dummy_cpu = 11;
    ProcessPtr proc_most_recently_run = dummy_process(1000, dummy_cpu, PROC_STATE_READY);
    ProcessPtr proc_least_recently_run = dummy_process(0, dummy_cpu, PROC_STATE_READY);

    Scheduler__start(scheduler, dummy_cpu);

    Scheduler__schedule(scheduler, proc_most_recently_run);
    Scheduler__schedule(scheduler, proc_least_recently_run);

    Scheduler__preempt(scheduler, dummy_cpu, context);
    TEST_ASSERT_EQUAL(proc_least_recently_run, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_READY, proc_most_recently_run->state);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc_least_recently_run->state);

    Scheduler__preempt(scheduler, dummy_cpu, context);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc_most_recently_run->state);
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, proc_least_recently_run->state);
    TEST_ASSERT_EQUAL(proc_most_recently_run, current_running_process());

}

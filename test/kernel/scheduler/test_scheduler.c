#include "unity.h"
#include "../stubs/panic.h"
#include "../stubs/cpu.h"
#include "../stubs/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/bin/embedded_binaries.h"

static SchedulerPtr scheduler;
static const CPUID cpu_id = 12;

void setUp(void)
{
    clear_panic_state();
    set_cpu_id(cpu_id);
    current_process_reset();

    scheduler = Scheduler__heap_new();
    ElfPtr idle = EmbeddedElf__load("idle");
    Scheduler__init(scheduler, idle);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
    scheduler = NULL;
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

ProcessPtr dummy_process(Tick last_tick, CPUID cpu, ProcessState state)
{
    ElfPtr hello_world = EmbeddedElf__load("hello_world");
    ResultMatch(ProcessPtr, Process__setup(hello_world))
        IsSuccess(process,
        {
            process->last_tick = last_tick;
            process->pid = last_tick * MAX_CPUS + cpu;
            process->state = state;

            return process;
        });
        IsError(code, message, TEST_FAIL())
    End
    return NULL;
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

void test_scheduler_prempt_when_idle_with_multiple_processes_ready_selects_longest_waiting(void)
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

#include "unity.h"
#include "../../../kernel/syscall/exit_handler.h"
#include "../fixtures/process.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/process.h"
#include "../../../kernel/arch/process.h"
#include "../../../kernel/scheduler/scheduler.h"


#define STUB_KERNEL \
    (Kernel) { \
        .scheduler = NEW_SCHEDULER \
    }

static ProcessPtr process = NULL;
static Kernel kernel = STUB_KERNEL;
static CPUID cpu = 11;
static ProcessContextPtr context = (ProcessContextPtr) 0x12345;

void setUp(void)
{
    process = dummy_process(100, cpu, PROC_STATE_RUNNING);

    Scheduler__init(&kernel.scheduler, EmbeddedElf__load("hello_world"));
    Scheduler__start(&kernel.scheduler, cpu);
    Scheduler__schedule(&kernel.scheduler, process);

    Scheduler__preempt(&kernel.scheduler, cpu, context);
}

void tearDown(void)
{

}

void test_exit_handler_interrupts_and_removes_process_from_scheduler(void)
{
    Optional(ProcessPtr) current_opt = Scheduler__current_process(&kernel.scheduler, cpu);
    Optional(ProcessPtr) idle_opt = Scheduler__idle_process(&kernel.scheduler, cpu);
    ProcessPtr idle = UnwrapPanic(ProcessPtr, idle_opt);

    Match(ProcessPtr, current_opt)
        IsNone(TEST_FAIL())
        IsSome(current, TEST_ASSERT_EQUAL(process, current));
    End

    Exit__handler(&kernel, process, 0, 0, 0, 0, 0);

    Match(ProcessPtr, Scheduler__current_process(&kernel.scheduler, cpu))
        IsNone(TEST_FAIL());
        IsSome(current, TEST_ASSERT_EQUAL(idle, current));
    End

    ProcessTablePtr processes = Scheduler__all_processes(&kernel.scheduler, cpu);

    Match(ArrayIndex, ProcessTable__find_item(processes, process))
        IsSome(val, TEST_FAIL());
    End
}
#include "unity.h"

#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/types/optional.h"

#include "../../../kernel/exec/exec.h"
#include "../fixtures/process.h"
#include "../fixtures/kernel.h"
#include "../stubs/process.h"
#include "../../../kernel/types/types.h"

static KernelPtr kernel = NULL;
static ProcessPtr parent = NULL;

static KString exec_path = KSTRING_NEW("/ebin/hello_world");

static OriginatingContext context;

static const CPUID cpu = 0;

void setUp(void)
{
    kernel = init_test_kernel();
    parent = dummy_process(0, cpu, PROC_STATE_RUNNING);

    CallContext syscall_context = CallContext__new(
        Some(ProcessPtr, parent), SYSCALL_EXEC, (Anything) cstr(exec_path), 0, 0, 0, 0
    );

    context = OriginatingContext__syscall(kernel, syscall_context);

    Scheduler__schedule(kernel->scheduler, parent);
    Scheduler__preempt(kernel->scheduler, cpu, NULL);
}

void tearDown(void)
{

}



void test_exec_spawns_process_and_puts_parent_in_wait()
{
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
    TEST_ASSERT_EQUAL(1, Scheduler__process_count(kernel->scheduler));
    ResultMatch(ProcessPtr, Exec__exec(context, exec_path, true, ProcessArguments__new()))
        IsError(code, message,
        {
            TEST_FAIL();
        });
        IsSuccess(spawned_process,
        {
            ProcessPtr spawned_parent = UnwrapPanic(ProcessPtr, spawned_process->parent);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);
            TEST_ASSERT_EQUAL(parent, spawned_parent);
            TEST_ASSERT_EQUAL(2, Scheduler__process_count(kernel->scheduler));
        })
    End
}

void test_exec_spawns_process_and_sets_up_a_defferred_return()
{
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
    TEST_ASSERT_EQUAL(1, Scheduler__process_count(kernel->scheduler));
    ReturnCode return_code = 55;

    ResultMatch(ProcessPtr, Exec__exec(context, exec_path, true, ProcessArguments__new()))
        IsError(code, message,
        {
            TEST_FAIL();
        });
        IsSuccess(spawned_process,
        {
            Scheduler__exit(kernel->scheduler, spawned_process, return_code);
            Scheduler__preempt(kernel->scheduler, cpu, NULL);

            TEST_ASSERT_EQUAL(return_code, ResultUnwrapPanic(Anything, deferred_syscall_result()));
        })
    End
}

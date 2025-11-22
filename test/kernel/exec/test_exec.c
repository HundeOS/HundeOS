#include "unity.h"

#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/types/optional.h"

#include "../../../kernel/exec/exec.h"
#include "../fixtures/process.h"
#include "../fixtures/kernel.h"


static KernelPtr kernel = NULL;
static ProcessPtr parent = NULL;

static KString exec_path = KSTRING_NEW("/embedded/hello_world");

static OriginatingContext context;

void setUp(void)
{
    kernel = init_test_kernel();
    parent = dummy_process(0, 0, PROC_STATE_RUNNING);

    CallContext syscall_context = CallContext__new(
        Some(ProcessPtr, parent), SYSCALL_EXEC, (Anything)cstr(exec_path), 0, 0, 0, 0
    );

    context = OriginatingContext__syscall(kernel, syscall_context);

    Scheduler__schedule(kernel->scheduler, parent);
    Scheduler__preempt(kernel->scheduler, 0, NULL);
}

void tearDown(void)
{

}



void test_exec_spawns_process_and_puts_parent_in_wait()
{
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
    TEST_ASSERT_EQUAL(1, Scheduler__process_count(kernel->scheduler));
    ResultMatch(ProcessPtr, Exec__exec(context, exec_path, true))
        IsError(code, message,
        {
            k_println(cstr(message));
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
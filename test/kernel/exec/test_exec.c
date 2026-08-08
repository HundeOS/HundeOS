#include "unity.h"

#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/types/optional.h"

#include "../../../kernel/exec/exec.h"
#include "../fixtures/process.h"
#include "../fixtures/kernel.h"
#include "../stubs/cpu.h"
#include "../stubs/process.h"
#include "../../../kernel/types/types.h"

static KernelPtr kernel = NULL;
static ProcessPtr parent = NULL;

static BString exec_path = BSTRING_NEW("/sbin/hello_world");

static OriginatingContext context;

static const CPUID cpu = 0;

void setUp(void)
{
    reset_cpu_count();
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
    reset_cpu_count();
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
            ProcessPtr spawned_parent = Unwrap(ProcessPtr, spawned_process->parent, (ProcessPtr){});
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

            TEST_ASSERT_EQUAL(return_code, ResultUnwrap(Anything, deferred_syscall_result(), (Anything){}));
        })
    End
}

void test_exec_wait_schedules_child_on_parent_cpu(void)
{
    set_cpu_count(2);
    Scheduler__start(kernel->scheduler, 1);

    ProcessPtr cpu1_parent = dummy_process(1, 1, PROC_STATE_RUNNING);
    BString path = BSTRING_NEW("/sbin/hello_world");
    CallContext syscall_context = CallContext__new(
        Some(ProcessPtr, cpu1_parent), SYSCALL_EXEC, (Anything) cstr(path), 0, 0, 0, 0
    );
    OriginatingContext cpu1_context = OriginatingContext__syscall(kernel, syscall_context);

    Scheduler__schedule(kernel->scheduler, cpu1_parent);
    Scheduler__preempt(kernel->scheduler, 1, NULL);

    ResultMatch(ProcessPtr, Exec__exec(cpu1_context, path, true, ProcessArguments__new()))
        IsError(code, message,
        {
            TEST_FAIL();
        });
        IsSuccess(spawned_process,
        {
            ProcessTablePtr cpu1_processes = Scheduler__all_processes(kernel->scheduler, 1);
            Match(ArrayIndex, ProcessTable__find_item(cpu1_processes, spawned_process))
                IsSome(index, {})
                IsNone(TEST_FAIL())
            End

            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, cpu1_parent->state);
        })
    End
}

void test_exec_marks_hundeschau_as_hund_identity(void)
{
    BString path = BSTRING_NEW("hundeschau");
    CallContext syscall_context = CallContext__new(
        Some(ProcessPtr, parent), SYSCALL_EXEC, (Anything) cstr(path), 0, 0, 0, 0
    );
    OriginatingContext hundeschau_context = OriginatingContext__syscall(kernel, syscall_context);

    ResultMatch(ProcessPtr, Exec__exec(hundeschau_context, path, false, ProcessArguments__new()))
        IsError(code, message, TEST_FAIL())
        IsSuccess(spawned_process,
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, spawned_process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, spawned_process->group_id);
        })
    End
}

void test_exec_apply_builtin_identity_policy_marks_boot_hundeschau_as_hund(void)
{
    ProcessPtr process = dummy_process(0, cpu, PROC_STATE_NEW);
    process->name = BSTRING_NEW("hundeschau");

    Exec__apply_builtin_identity_policy(process);

    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, process->user_id);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, process->group_id);
}

void test_exec_as_can_spawn_shell_as_user(void)
{
    BString path = BSTRING_NEW("/sbin/shell");

    ResultMatch(ProcessPtr, Exec__exec_as(
        context,
        path,
        false,
        Some(UserID, PROCESS_USER_USER),
        ProcessArguments__new()
    ))
        IsError(code, message, TEST_FAIL())
        IsSuccess(spawned_process,
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_USER, spawned_process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_USER, spawned_process->group_id);
        })
    End
}

void test_exec_as_denies_unprivileged_identity_switch(void)
{
    Process__set_user_identity(parent);
    BString path = BSTRING_NEW("/sbin/shell");

    ResultMatch(ProcessPtr, Exec__exec_as(
        context,
        path,
        false,
        Some(UserID, PROCESS_USER_HUND),
        ProcessArguments__new()
    ))
        IsError(code, message, TEST_ASSERT_EQUAL(SPAWN_FAILED, code))
        IsSuccess(spawned_process, TEST_FAIL())
    End
}

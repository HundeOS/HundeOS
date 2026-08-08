#include "unity.h"

#include "../../../kernel/syscall/process_handler.h"
#include "../../../kernel/syscall/init.h"
#include "../../../kernel/syscall/signal_handler.h"
#include "../../../kernel/syscall/shutdown_handler.h"
#include "../../../kernel/syscall/syscall_dispatcher.h"
#include "../../../common_includes/syscalls/process.h"
#include "../../../common_includes/syscalls/calls.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/system.h"

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;

static bool scheduler_contains_process(ProcessID pid)
{
    Match(ProcessPtr, Scheduler__find_process_by_pid(kernel->scheduler, pid))
        IsSome(process, return true)
    End

    return false;
}

void setUp(void)
{
    clear_panic_state();
    reset_system_state();
    kernel = init_test_kernel_without_filesystem();
    Init__syscalls(&kernel->syscall_dispatcher);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_process_list_handler_returns_scheduled_processes(void)
{
    ProcessPtr init = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr driver = dummy_process(2, cpu, PROC_STATE_READY);
    driver->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(driver);

    Scheduler__schedule(kernel->scheduler, init);
    Scheduler__schedule(kernel->scheduler, driver);
    KernelContext kernel_context = KernelContext__new(Some(ProcessID, driver->pid), Some(SchedulerPtr, kernel->scheduler));
    Scheduler__park(kernel->scheduler, driver, &kernel_context, None(Continuation), PARK_WAIT_SERVICE_EVENT);

    ProcessInfo entries[4];
    ProcessListRequest request = (ProcessListRequest) {
        .entries = entries,
        .max_entries = 4,
    };

    ResultMatch(SystemCallResult, ProcessList__handler(
        kernel, Some(ProcessPtr, init), (Anything) &request, 0, 0, 0, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_UINT64(3, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_RUNNING", entries[0].state);
            TEST_ASSERT_EQUAL_STRING("-", entries[0].wait_reason);
            TEST_ASSERT_EQUAL_STRING("normal", entries[0].class_name);
            TEST_ASSERT_EQUAL_STRING("root", entries[0].user_name);
            TEST_ASSERT_EQUAL_UINT64(init->pid, entries[1].pid);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_READY", entries[1].state);
            TEST_ASSERT_EQUAL_STRING("-", entries[1].wait_reason);
            TEST_ASSERT_EQUAL_STRING("normal", entries[1].class_name);
            TEST_ASSERT_EQUAL_STRING("root", entries[1].user_name);
            TEST_ASSERT_EQUAL_UINT64(driver->pid, entries[2].pid);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_WAITING", entries[2].state);
            TEST_ASSERT_EQUAL_STRING("PARK_WAIT_SERVICE_EVENT", entries[2].wait_reason);
            TEST_ASSERT_EQUAL_STRING("service", entries[2].class_name);
            TEST_ASSERT_EQUAL_STRING("hund", entries[2].user_name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_process_list_handler_respects_entry_limit(void)
{
    ProcessPtr first = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr second = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, first);
    Scheduler__schedule(kernel->scheduler, second);

    ProcessInfo entries[1];
    ProcessListRequest request = (ProcessListRequest) {
        .entries = entries,
        .max_entries = 1,
    };

    ResultMatch(SystemCallResult, ProcessList__handler(
        kernel, Some(ProcessPtr, first), (Anything) &request, 0, 0, 0, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_UINT64(1, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("root", entries[0].user_name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_process_list_handler_rejects_missing_request(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);

    ResultMatch(SystemCallResult, ProcessList__handler(
        kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0
    ))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(PROCESS_LIST_INVALID_ARGUMENT, code))
    End
}

void test_signal_handler_terminates_ready_process_immediately(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);
    Scheduler__schedule(kernel->scheduler, target);

    ResultMatch(SystemCallResult, Signal__handler(
        kernel, Some(ProcessPtr, sender), (Anything) target->pid, (Anything) PROCESS_SIGNAL_TERMINATE, 0, 0, 0
    ))
        IsSuccess(result,
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, target->state);
            TEST_ASSERT_FALSE(scheduler_contains_process(target->pid));
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_signal_handler_queues_running_process_until_next_syscall_boundary(void)
{
    ProcessPtr target = dummy_process(1, cpu, PROC_STATE_RUNNING);

    Scheduler__schedule(kernel->scheduler, target);
    target->state = PROC_STATE_RUNNING;

    ResultMatch(SystemCallResult, Signal__handler(
        kernel, Some(ProcessPtr, target), (Anything) target->pid, (Anything) PROCESS_SIGNAL_KILL, 0, 0, 0
    ))
        IsSuccess(result,
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
            TEST_ASSERT_TRUE(Process__has_pending_signal(target));
            TEST_ASSERT_TRUE(scheduler_contains_process(target->pid));
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, SyscallDispatcher__dispatch(
        &kernel->syscall_dispatcher, kernel, Some(ProcessPtr, target), PING, 0, 0, 0, 0, 0
    ))
        IsSuccess(result,
        {
            TEST_ASSERT_EQUAL(SYSCALL_OK, result);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, target->state);
            TEST_ASSERT_FALSE(Process__has_pending_signal(target));
            TEST_ASSERT_FALSE(scheduler_contains_process(target->pid));
        })
        IsError(code, message, TEST_FAIL_MESSAGE(BString__cstr(&message)))
    End
}

void test_signal_handler_rejects_unsupported_signal(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);
    Scheduler__schedule(kernel->scheduler, target);

    ResultMatch(SystemCallResult, Signal__handler(
        kernel, Some(ProcessPtr, sender), (Anything) target->pid, (Anything) 99, 0, 0, 0
    ))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(PROCESS_SIGNAL_UNSUPPORTED, code))
    End
}

void test_process_wait_child_event_handler_returns_queued_child_event(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessChildEvent event = (ProcessChildEvent) {
        .pid = 42,
        .status = -2009,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    Scheduler__schedule(kernel->scheduler, parent);
    ResultMatch(CollectionCode, Process__push_child_event(parent, event))
        IsSuccess(result, TEST_ASSERT_EQUAL_UINT8(0, result))
        IsError(code, message, TEST_FAIL())
    End

    ProcessChildEvent target = { 0 };

    ResultMatch(SystemCallResult, ProcessWaitChildEvent__handler(
        kernel, Some(ProcessPtr, parent), (Anything) &target, 0, 0, 0, 0
    ))
        IsSuccess(result,
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(event.pid, target.pid);
            TEST_ASSERT_EQUAL_INT64(event.status, target.status);
            TEST_ASSERT_EQUAL_UINT64(event.type, target.type);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_shutdown_handler_invokes_system_shutdown(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);

    ResultMatch(SystemCallResult, Shutdown__handler(
        kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0
    ))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message,
        {
            TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code);
            TEST_ASSERT_TRUE(system_shutdown_called());
        })
    End
}

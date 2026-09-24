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

#define ProcessList__handler(kernel, proc, ...) ProcessList__handler((kernel), &(proc), __VA_ARGS__)
#define Signal__handler(kernel, proc, ...) Signal__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessWaitChildEvent__handler(kernel, proc, ...) ProcessWaitChildEvent__handler((kernel), &(proc), __VA_ARGS__)
#define Shutdown__handler(kernel, proc, ...) Shutdown__handler((kernel), &(proc), __VA_ARGS__)

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;
extern uint64_t test_cpu_time_usec;

static bool scheduler_contains_process(ProcessID pid)
{
    match_optional(
        Scheduler__find_process_by_pid(kernel->scheduler, pid),
        some(process)
        {
            return true;
        },
        none_do_nothing()
    );

    return false;
}

void setUp(void)
{
    test_cpu_time_usec = 0;
    clear_panic_state();
    reset_system_state();
    kernel = init_test_kernel();
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
    CPUUsage__start(&driver->cpu_usage, test_cpu_time_usec, 0);
    test_cpu_time_usec = 10000;
    CPUUsage__stop(&driver->cpu_usage, test_cpu_time_usec, 0);
    driver->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(driver);
    ProcessPtr ready_waiter = dummy_process(3, cpu, PROC_STATE_READY);
    ready_waiter->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(ready_waiter);

    Scheduler__schedule_on_cpu(kernel->scheduler, init, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, driver, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, ready_waiter, cpu);
    KernelContext kernel_context =
        KernelContext__new(&Some(ProcessID, driver->pid), &Some(SchedulerPtr, kernel->scheduler));
    Scheduler__park(kernel->scheduler, driver, &kernel_context, &None(Continuation), PARK_WAIT_SERVICE_EVENT);
    KernelContext ready_context =
        KernelContext__new(&Some(ProcessID, ready_waiter->pid), &Some(SchedulerPtr, kernel->scheduler));
    Scheduler__park(kernel->scheduler, ready_waiter, &ready_context, &None(Continuation), PARK_WAIT_SERVICE_READY);

    ProcessInfo entries[4];
    ProcessListRequest request = (ProcessListRequest){
        .entries = entries,
        .max_entries = 4,
    };

    match_result(
        ProcessList__handler(kernel, Some(ProcessPtr, init), (Anything)&request, 0, 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_UINT64(4, count);
            TEST_ASSERT_EQUAL_UINT64(cpu, entries[0].cpu);
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
            TEST_ASSERT_EQUAL_UINT64(cpu, entries[2].cpu);
            TEST_ASSERT_EQUAL_UINT64(10000, entries[2].cpu_usage.total_usec);
            TEST_ASSERT_EQUAL_UINT64(1000, entries[2].cpu_usage.short_basis_points);
            TEST_ASSERT_EQUAL_UINT64(100, entries[2].cpu_usage.long_basis_points);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_WAITING", entries[2].state);
            TEST_ASSERT_EQUAL_STRING("PARK_WAIT_SERVICE_EVENT", entries[2].wait_reason);
            TEST_ASSERT_EQUAL_STRING("service", entries[2].class_name);
            TEST_ASSERT_EQUAL_STRING("hund", entries[2].user_name);
            TEST_ASSERT_EQUAL_UINT64(ready_waiter->pid, entries[3].pid);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_WAITING", entries[3].state);
            TEST_ASSERT_EQUAL_STRING("PARK_WAIT_SERVICE_READY", entries[3].wait_reason);
            TEST_ASSERT_EQUAL_STRING("service", entries[3].class_name);
            TEST_ASSERT_EQUAL_STRING("hund", entries[3].user_name);
        },
        error_test_fail()
    );
}

void test_process_list_observes_child_wait_without_registry_timer_refresh(void)
{
    ProcessPtr shell = dummy_process(40, cpu, PROC_STATE_READY);
    ProcessPtr child = dummy_process(41, cpu, PROC_STATE_READY);
    Scheduler__schedule_on_cpu(kernel->scheduler, shell, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, child, cpu);
    shell->state = PROC_STATE_RUNNING;
    KernelContext context = KernelContext__new(&Some(ProcessID, shell->pid), &Some(SchedulerPtr, kernel->scheduler));
    match_result(Scheduler__park(kernel->scheduler, shell, &context, &None(Continuation), PARK_WAIT_CHILD),
                 success_do_nothing(),
        error_test_fail()
    );
    child->state = PROC_STATE_RUNNING;
    ProcessInfo entries[3];
    ProcessListRequest request = ProcessListRequest__new(entries, 3);
    match_result(
        ProcessList__handler(kernel, Some(ProcessPtr, child), (Anything)&request, 0, 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_UINT64(3, count);
            TEST_ASSERT_EQUAL_UINT64(shell->pid, entries[1].pid);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_WAITING", entries[1].state);
            TEST_ASSERT_EQUAL_STRING("PARK_WAIT_CHILD", entries[1].wait_reason);
            TEST_ASSERT_EQUAL_UINT64(child->pid, entries[2].pid);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_RUNNING", entries[2].state);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(mutex_trylock(&kernel->scheduler->scheduler_lock));
    mutex_unlock(&kernel->scheduler->scheduler_lock);
}

void test_process_list_handler_respects_entry_limit(void)
{
    ProcessPtr first = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr second = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, first);
    Scheduler__schedule(kernel->scheduler, second);

    ProcessInfo entries[1];
    ProcessListRequest request = (ProcessListRequest){
        .entries = entries,
        .max_entries = 1,
    };

    match_result(
        ProcessList__handler(kernel, Some(ProcessPtr, first), (Anything)&request, 0, 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_UINT64(1, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("root", entries[0].user_name);
        },
        error_test_fail()
    );
}

void test_process_list_handler_rejects_missing_request(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);

    match_result(
        ProcessList__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_LIST_INVALID_ARGUMENT, code);
        }
    );
}

void test_process_credentials_returns_target_identity_not_requester_identity(void)
{
    ProcessPtr requester = dummy_process(41, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(42, cpu, PROC_STATE_READY);
    Process__set_hund_identity(requester);
    Process__set_identity(target, 123, 456, PROCESS_CAP_NONE);
    Scheduler__schedule(kernel->scheduler, target);
    ProcessCredentials credentials = ProcessCredentials__new(0, 0);
    match_result(
        ProcessCredentials__handler(kernel, &Some(ProcessPtr, requester), target->pid, (Anything)&credentials, 0, 0, 0),
        success(status)
        {
            TEST_ASSERT_EQUAL(PROCESS_CREDENTIALS_OK, status);
            TEST_ASSERT_EQUAL(123, credentials.user_id);
            TEST_ASSERT_EQUAL(456, credentials.group_id);
        },
        error_test_fail()
    );
}

void test_process_credentials_rejects_missing_pid_and_invalid_arguments_without_overwriting_output(void)
{
    ProcessCredentials credentials = ProcessCredentials__new(123, 456);
    match_result(
        ProcessCredentials__handler(kernel, &None(ProcessPtr), 99999, (Anything)&credentials, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CREDENTIALS_NOT_FOUND, code);
        }
    );
    TEST_ASSERT_EQUAL(123, credentials.user_id);
    TEST_ASSERT_EQUAL(456, credentials.group_id);
    match_result(
        ProcessCredentials__handler(kernel, &None(ProcessPtr), 0, (Anything)&credentials, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CREDENTIALS_INVALID_ARGUMENT, code);
        }
    );
    match_result(
        ProcessCredentials__handler(kernel, &None(ProcessPtr), 1, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CREDENTIALS_INVALID_ARGUMENT, code);
        }
    );
}

void test_signal_handler_queues_ready_process_without_teardown(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);
    Scheduler__schedule(kernel->scheduler, target);

    match_result(
        Signal__handler(kernel, Some(ProcessPtr, sender), (Anything)target->pid, (Anything)PROCESS_SIGNAL_TERMINATE, 0,
                        0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
            TEST_ASSERT_EQUAL(PROC_STATE_READY, target->state);
            TEST_ASSERT_TRUE(Process__has_pending_signal(target));
            TEST_ASSERT_TRUE(scheduler_contains_process(target->pid));
        },
        error_test_fail()
    );
}

void test_signal_handler_queues_running_process_until_next_syscall_boundary(void)
{
    ProcessPtr target = dummy_process(1, cpu, PROC_STATE_RUNNING);

    Scheduler__schedule_on_cpu(kernel->scheduler, target, cpu);
    target->state = PROC_STATE_RUNNING;

    match_result(
        Signal__handler(kernel, Some(ProcessPtr, target), (Anything)target->pid, (Anything)PROCESS_SIGNAL_KILL, 0, 0,
                        0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
            TEST_ASSERT_TRUE(Process__has_pending_signal(target));
            TEST_ASSERT_TRUE(scheduler_contains_process(target->pid));
        },
        error_test_fail()
    );

    match_result(
        SyscallDispatcher__dispatch(&kernel->syscall_dispatcher, kernel, &Some(ProcessPtr, target), PING, 0, 0, 0, 0,
                                    0),
        success(result)
        {
            TEST_ASSERT_EQUAL(SYSCALL_OK, result);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, target->state);
            TEST_ASSERT_FALSE(Process__has_pending_signal(target));
            TEST_ASSERT_FALSE(scheduler_contains_process(target->pid));
        },
        error(code, message)
        {
            TEST_FAIL_MESSAGE(BString__cstr(&message));
        }
    );
}

void test_signal_handler_rejects_unsupported_signal(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);
    Scheduler__schedule(kernel->scheduler, target);

    match_result(
        Signal__handler(kernel, Some(ProcessPtr, sender), (Anything)target->pid, (Anything)99, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_UNSUPPORTED, code);
        }
    );
}

void test_process_wait_child_event_handler_returns_queued_child_event(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessChildEvent event = (ProcessChildEvent){
        .pid = 42,
        .status = -2009,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    Scheduler__schedule(kernel->scheduler, parent);
    match_result(
        Process__push_child_event(parent, &event),
        success(result)
        {
            TEST_ASSERT_EQUAL_UINT8(0, result);
        },
        error_test_fail()
    );

    ProcessChildEvent target = ProcessChildEvent__new();

    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(event.pid, target.pid);
            TEST_ASSERT_EQUAL_INT64(event.status, target.status);
            TEST_ASSERT_EQUAL_UINT64(event.type, target.type);
        },
        error_test_fail()
    );
}

void test_shutdown_handler_invokes_system_shutdown(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);

    match_result(
        Shutdown__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_FAILURE, code);
            TEST_ASSERT_TRUE(system_shutdown_called());
        }
    );
}

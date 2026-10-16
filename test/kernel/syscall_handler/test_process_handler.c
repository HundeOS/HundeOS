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
#include "../stubs/cpu.h"
#include "../stubs/panic.h"
#include "../stubs/system.h"
#include "../stubs/process_context_layout.h"
#include "../stubs/kalloc.h"
#include "../stubs/page_table.h"

#define ProcessList__handler(kernel, proc, ...) ProcessList__handler((kernel), &(proc), __VA_ARGS__)
#define Signal__handler(kernel, proc, ...) Signal__handler((kernel), &(proc), __VA_ARGS__)
#define SignalControl__handler(kernel, proc, ...) SignalControl__handler((kernel), &(proc), __VA_ARGS__)
#define SignalReturn__handler(kernel, proc, ...) SignalReturn__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessWaitChildEvent__handler(kernel, proc, ...) ProcessWaitChildEvent__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessIdleWait__handler(kernel, proc, ...) ProcessIdleWait__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessTimes__handler(kernel, proc, ...) ProcessTimes__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessIdentity__handler(kernel, proc, ...) ProcessIdentity__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessGroup__handler(kernel, proc, ...) ProcessGroup__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessFork__handler(kernel, proc, ...) ProcessFork__handler((kernel), &(proc), __VA_ARGS__)
#define ProcessRegistryMap__handler(kernel, proc, ...) ProcessRegistryMap__handler((kernel), &(proc), __VA_ARGS__)
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
    CPUStub__reset_wait_count();
    kernel = init_test_kernel();
    Init__syscalls(&kernel->syscall_dispatcher);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_process_registry_map_handler_maps_once_and_returns_userspace_address(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_RUNNING);

    match_result(
        ProcessRegistryMap__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_REGISTRY_VADDR, result);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(process->process_registry_mapped);
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), PROCESS_REGISTRY_VADDR,
                                          true, false, false, PROCESS_REGISTRY_SIZE));

    match_result(
        ProcessRegistryMap__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_REGISTRY_VADDR, result);
        },
        error_test_fail()
    );
}

void test_process_registry_map_handler_rejects_missing_caller(void)
{
    match_result(
        ProcessRegistryMap__handler(kernel, None(ProcessPtr), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_REGISTRY_MAP_INVALID_CALLER, code);
        }
    );
}

void test_process_idle_wait_handler_yields_and_waits_for_idle_process(void)
{
    ProcessPtr idle_process = unwrap_optional(Scheduler__idle_process(kernel->scheduler, cpu), (ProcessPtr){});

    match_result(
        ProcessIdleWait__handler(kernel, Some(ProcessPtr, idle_process), 0, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDLE_WAIT_OK, result);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(1, CPUStub__wait_count());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, idle_process->state);
}

void test_process_idle_wait_handler_rejects_non_idle_process(void)
{
    ProcessPtr process = dummy_process(71, cpu, PROC_STATE_RUNNING);

    match_result(
        ProcessIdleWait__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDLE_WAIT_INVALID_CALLER, code);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(0, CPUStub__wait_count());
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

void test_process_fork_handler_publishes_complete_child_only_after_resume(void)
{
    ProcessPtr parent = dummy_process(60, cpu, PROC_STATE_RUNNING);
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    parent->state = PROC_STATE_RUNNING;

    ProcessID child_pid = 0;
    match_result(
        ProcessFork__handler(kernel, Some(ProcessPtr, parent), PROCESS_FORK_PREPARE, 0, 0, 0, 0),
        success(prepared_pid)
        {
            child_pid = (ProcessID)prepared_pid;
        },
        error_test_fail()
    );
    TEST_ASSERT_NOT_EQUAL(0, child_pid);
    TEST_ASSERT_FALSE(scheduler_contains_process(child_pid));

    match_result(
        ProcessFork__handler(kernel, Some(ProcessPtr, parent), PROCESS_FORK_RESUME, child_pid, 0, 0, 0),
        success(resumed_pid)
        {
            TEST_ASSERT_EQUAL_UINT64(child_pid, resumed_pid);
        },
        error_test_fail()
    );
    match_optional(
        Scheduler__find_process_by_pid(kernel->scheduler, child_pid),
        some(child)
        {
            TEST_ASSERT_EQUAL_UINT64(parent->pid, unwrap_optional(child->parent_pid, 0));
            TEST_ASSERT_EQUAL(PROC_STATE_READY, child->state);
            TEST_ASSERT_EQUAL_UINT64(0, Process__required_context(child)->x[0]);
        },
        none()
        {
            TEST_FAIL_MESSAGE("fork child was not published");
        }
    );
}

void test_process_fork_handler_aborts_unpublished_child(void)
{
    ProcessPtr parent = dummy_process(62, cpu, PROC_STATE_RUNNING);
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    parent->state = PROC_STATE_RUNNING;
    ProcessID child_pid = 0;

    match_result(
        ProcessFork__handler(kernel, Some(ProcessPtr, parent), PROCESS_FORK_PREPARE, 0, 0, 0, 0),
        success(prepared_pid)
        {
            child_pid = (ProcessID)prepared_pid;
        },
        error_test_fail()
    );
    TEST_ASSERT_FALSE(scheduler_contains_process(child_pid));
    match_result(
        ProcessFork__handler(kernel, Some(ProcessPtr, parent), PROCESS_FORK_ABORT, child_pid, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL_UINT64(0, result);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(parent->pending_fork_child));
    TEST_ASSERT_FALSE(scheduler_contains_process(child_pid));
}

void test_process_fork_handler_rejects_missing_caller(void)
{
    match_result(
        ProcessFork__handler(kernel, None(ProcessPtr), PROCESS_FORK_PREPARE, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_FORK_INVALID_CALLER, code);
        }
    );
}

void test_process_fork_handler_rolls_back_allocation_failure_before_publication(void)
{
    ProcessPtr parent = dummy_process(61, cpu, PROC_STATE_RUNNING);
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    parent->state = PROC_STATE_RUNNING;
    uint64_t process_count = Scheduler__process_count(kernel->scheduler);
    size_t frees_before = KAllocStub__free_count();
    KAllocStub__set_user_alloc_failure(true);

    match_result(
        ProcessFork__handler(kernel, Some(ProcessPtr, parent), PROCESS_FORK_PREPARE, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_FORK_NO_MEMORY, code);
        }
    );

    KAllocStub__set_user_alloc_failure(false);
    TEST_ASSERT_EQUAL_UINT64(process_count, Scheduler__process_count(kernel->scheduler));
    TEST_ASSERT_GREATER_THAN(frees_before, KAllocStub__free_count());
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

void test_signal_handler_accepts_abort_signal(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);
    Scheduler__schedule(kernel->scheduler, target);

    match_result(
        Signal__handler(kernel, Some(ProcessPtr, sender), (Anything)target->pid, (Anything)PROCESS_SIGNAL_ABORT, 0, 0,
                        0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
            TEST_ASSERT_TRUE(Process__has_pending_signal(target));
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
        },
        error(code, message)
        {
            TEST_FAIL_MESSAGE(BString__cstr(&message));
        }
    );

    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_TERMINATE,
                      Scheduler__deliver_pending_signal(kernel->scheduler, target, Process__required_context(target)));
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, target->state);
    TEST_ASSERT_FALSE(Process__has_pending_signal(target));
    TEST_ASSERT_FALSE(scheduler_contains_process(target->pid));
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
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_INVALID_ARGUMENT, code);
        }
    );
}

void test_signal_handler_zero_probes_process_without_queuing_signal(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr target = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);
    Scheduler__schedule(kernel->scheduler, target);

    match_result(
        Signal__handler(kernel, Some(ProcessPtr, sender), (Anything)target->pid, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
            TEST_ASSERT_FALSE(Process__has_pending_signal(target));
            TEST_ASSERT_TRUE(scheduler_contains_process(target->pid));
        },
        error_test_fail()
    );
}

void test_signal_handler_zero_rejects_missing_process(void)
{
    ProcessPtr sender = dummy_process(1, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, sender);

    match_result(
        Signal__handler(kernel, Some(ProcessPtr, sender), 999, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_PROCESS_NOT_FOUND, code);
        }
    );
}

void test_signal_control_handler_installs_actions_and_manages_the_process_mask(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_RUNNING);
    ProcessSignalControlRequest action_request = {
        .operation = PROCESS_SIGNAL_CONTROL_ACTION,
        .signal = PROCESS_SIGNAL_INTERRUPT,
        .replace = 1,
        .action =
            {
                .handler = 0x12340000,
                .restorer = 0x56780000,
                .mask = 1ULL << (PROCESS_SIGNAL_USER_1 - 1),
                .flags = PROCESS_SIGNAL_ACTION_RESTART,
            },
    };

    match_result(
        SignalControl__handler(kernel, Some(ProcessPtr, process), (Anything)&action_request, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_DEFAULT_HANDLER, action_request.previous_action.handler);
    TEST_ASSERT_EQUAL_UINT64(action_request.action.handler,
                             Process__signal_action(process, PROCESS_SIGNAL_INTERRUPT).handler);

    ProcessSignalMask blocked = 1ULL << (PROCESS_SIGNAL_USER_2 - 1);
    ProcessSignalControlRequest mask_request = {
        .operation = PROCESS_SIGNAL_CONTROL_MASK,
        .replace = 1,
        .mask_operation = PROCESS_SIGNAL_MASK_BLOCK,
        .mask = blocked,
    };
    match_result(SignalControl__handler(kernel, Some(ProcessPtr, process), (Anything)&mask_request, 0, 0, 0, 0),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0, mask_request.previous_mask);
    TEST_ASSERT_EQUAL_UINT64(blocked, Process__signal_mask(process));

    Process__queue_signal(process, PROCESS_SIGNAL_USER_2);
    ProcessSignalControlRequest pending_request = {
        .operation = PROCESS_SIGNAL_CONTROL_PENDING,
    };
    match_result(SignalControl__handler(kernel, Some(ProcessPtr, process), (Anything)&pending_request, 0, 0, 0, 0),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(blocked, pending_request.previous_mask);
}

void test_signal_return_handler_restores_the_interrupted_context(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_RUNNING);
    ProcessSignalAction action = {
        .handler = 0x12340000,
        .restorer = 0x56780000,
    };
    ProcessContext interrupted = {
        .x = {0x1122},
        .sp = 0x3344,
        .elr = 0x5566,
    };
    match_result(Process__set_signal_action(process, PROCESS_SIGNAL_INTERRUPT, &action),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);
    match_result(Process__deliver_pending_signal(process, &interrupted),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        SignalReturn__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(SYSCALL_CONTEXT_RESTORED, result);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0x1122, Process__required_context(process)->x[0]);
    TEST_ASSERT_EQUAL_UINT64(0x3344, Process__required_context(process)->sp);
    TEST_ASSERT_EQUAL_UINT64(0x5566, Process__required_context(process)->elr);
}

void test_only_signal_return_result_restores_the_process_context(void)
{
    TEST_ASSERT_TRUE(SystemCallResult__restores_context(SYSCALL_PROCESS_SIGNAL_RETURN, SYSCALL_CONTEXT_RESTORED));
    TEST_ASSERT_FALSE(SystemCallResult__restores_context(SYSCALL_AUTH, SYSCALL_CONTEXT_RESTORED));
    TEST_ASSERT_FALSE(SystemCallResult__restores_context(SYSCALL_PROCESS_SIGNAL_RETURN, SYSCALL_OK));
}

void test_signal_control_alarm_replaces_timer_and_suspend_parks_atomically(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    Scheduler__schedule_on_cpu(kernel->scheduler, process, cpu);
    test_cpu_time_usec = 1000000;
    ProcessSignalControlRequest alarm = {
        .operation = PROCESS_SIGNAL_CONTROL_ALARM,
        .alarm_seconds = 5,
    };
    match_result(
        SignalControl__handler(kernel, Some(ProcessPtr, process), (Anything)&alarm, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_OK, result);
        },
        error_test_fail()
    );
    test_cpu_time_usec = 3200000;
    alarm.alarm_seconds = 7;
    match_result(SignalControl__handler(kernel, Some(ProcessPtr, process), (Anything)&alarm, 0, 0, 0, 0),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(3, alarm.previous_alarm_seconds);

    ProcessSignalControlRequest suspend = {
        .operation = PROCESS_SIGNAL_CONTROL_SUSPEND,
        .mask = 1ULL << (PROCESS_SIGNAL_USER_1 - 1),
    };
    match_result(
        SignalControl__handler(kernel, Some(ProcessPtr, process), (Anything)&suspend, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_IO_BLOCK, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);
    TEST_ASSERT_TRUE(Process__has_parked_operation(process, PARK_WAIT_SIGNAL, &None(OperationKey)));
}

void test_process_group_handler_gets_sets_and_creates_session(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->session_id = 50;
    process->process_group_id = 60;
    Scheduler__schedule_on_cpu(kernel->scheduler, process, cpu);
    ProcessGroupRequest request = {
        .operation = PROCESS_GROUP_GET,
        .pid = 0,
    };
    match_result(
        ProcessGroup__handler(kernel, Some(ProcessPtr, process), (Anything)&request, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_GROUP_OK, result);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_INT64(60, request.result_id);

    request.operation = PROCESS_GROUP_SET;
    request.process_group_id = 0;
    match_result(ProcessGroup__handler(kernel, Some(ProcessPtr, process), (Anything)&request, 0, 0, 0, 0),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_INT64(process->pid, request.result_id);

    process->process_group_id = 60;
    request.operation = PROCESS_GROUP_CREATE_SESSION;
    match_result(ProcessGroup__handler(kernel, Some(ProcessPtr, process), (Anything)&request, 0, 0, 0, 0),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_INT64(process->pid, request.result_id);
}

void test_process_wait_child_event_handler_returns_queued_child_event(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessChildEvent event = (ProcessChildEvent){
        .pid = 42,
        .status = -2009,
        .type = PROCESS_CHILD_EVENT_EXITED,
        .user_usec = 120,
        .system_usec = 30,
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
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, (Anything)-1, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(event.pid, target.pid);
            TEST_ASSERT_EQUAL_INT64(event.status, target.status);
            TEST_ASSERT_EQUAL_UINT64(event.type, target.type);
            TEST_ASSERT_EQUAL_UINT64(event.user_usec, parent->children_user_usec);
            TEST_ASSERT_EQUAL_UINT64(event.system_usec, parent->children_system_usec);
        },
        error_test_fail()
    );
}

void test_process_times_handler_reports_current_and_waited_child_cpu_time(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_RUNNING);
    process->children_user_usec = 70;
    process->children_system_usec = 20;
    CPUUsage__start_mode(&process->cpu_usage, 10, 0, CPU_USAGE_USER);
    CPUUsage__transition(&process->cpu_usage, 30, 0, CPU_USAGE_SYSTEM);
    test_cpu_time_usec = 50;
    ProcessTimes times = ProcessTimes__new();

    match_result(
        ProcessTimes__handler(kernel, Some(ProcessPtr, process), (Anything)&times, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_TIMES_OK, result);
            TEST_ASSERT_EQUAL_UINT64(50, times.elapsed_usec);
            TEST_ASSERT_EQUAL_UINT64(20, times.user_usec);
            TEST_ASSERT_EQUAL_UINT64(20, times.system_usec);
            TEST_ASSERT_EQUAL_UINT64(70, times.children_user_usec);
            TEST_ASSERT_EQUAL_UINT64(20, times.children_system_usec);
        },
        error_test_fail()
    );
}

void test_process_times_handler_rejects_missing_output(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_RUNNING);

    match_result(
        ProcessTimes__handler(kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_TIMES_INVALID_ARGUMENT, code);
        }
    );
}

void test_process_identity_handler_reports_current_and_parent_process_ids(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr child = dummy_process_with_parent(2, cpu, PROC_STATE_RUNNING, parent);
    ProcessIdentity identity = ProcessIdentity__new(0, 0);

    match_result(
        ProcessIdentity__handler(kernel, Some(ProcessPtr, child), (Anything)&identity, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDENTITY_OK, result);
            TEST_ASSERT_EQUAL_UINT64(child->pid, identity.pid);
            TEST_ASSERT_EQUAL_UINT64(parent->pid, identity.parent_pid);
        },
        error_test_fail()
    );
}

void test_process_identity_handler_reports_zero_for_process_without_parent(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_RUNNING);
    ProcessIdentity identity = ProcessIdentity__new(0, 99);

    match_result(
        ProcessIdentity__handler(kernel, Some(ProcessPtr, process), (Anything)&identity, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDENTITY_OK, result);
            TEST_ASSERT_EQUAL_UINT64(process->pid, identity.pid);
            TEST_ASSERT_EQUAL_UINT64(0, identity.parent_pid);
        },
        error_test_fail()
    );
}

void test_process_identity_handler_rejects_missing_process_or_output(void)
{
    ProcessIdentity identity = ProcessIdentity__new(12, 34);

    match_result(
        ProcessIdentity__handler(kernel, None(ProcessPtr), (Anything)&identity, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDENTITY_INVALID_ARGUMENT, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT64(12, identity.pid);
    TEST_ASSERT_EQUAL_UINT64(34, identity.parent_pid);

    match_result(
        ProcessIdentity__handler(kernel, Some(ProcessPtr, dummy_process(1, cpu, PROC_STATE_RUNNING)), 0, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_IDENTITY_INVALID_ARGUMENT, code);
        }
    );
}

void test_process_wait_child_event_selects_child_without_consuming_other_event(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessChildEvent first = {
        .pid = 41,
        .status = PROCESS_WAIT_STATUS_EXITED(1),
        .type = PROCESS_CHILD_EVENT_EXITED,
    };
    ProcessChildEvent selected = {
        .pid = 42,
        .status = PROCESS_WAIT_STATUS_EXITED(2),
        .type = PROCESS_CHILD_EVENT_EXITED,
    };
    Scheduler__schedule(kernel->scheduler, parent);
    match_result(Process__push_child_event(parent, &first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__push_child_event(parent, &selected),
        success_do_nothing(),
        error_test_fail()
    );

    ProcessChildEvent target = ProcessChildEvent__new();
    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, selected.pid, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(selected.pid, target.pid);
        },
        error_test_fail()
    );

    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, first.pid, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(first.pid, target.pid);
        },
        error_test_fail()
    );
}

void test_process_wait_child_event_filters_stopped_and_continued_events_by_options(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr child = dummy_process(42, cpu, PROC_STATE_READY);
    child->parent = Some(ProcessPtr, parent);
    ProcessChildEvent stopped = {
        .pid = child->pid,
        .status = PROCESS_WAIT_STATUS_STOPPED(PROCESS_SIGNAL_TERMINAL_STOP),
        .type = PROCESS_CHILD_EVENT_STOPPED,
    };
    ProcessChildEvent continued = {
        .pid = child->pid,
        .status = PROCESS_WAIT_STATUS_CONTINUED,
        .type = PROCESS_CHILD_EVENT_CONTINUED,
    };
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    Scheduler__schedule(kernel->scheduler, child);
    match_result(Process__push_child_event(parent, &stopped),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__push_child_event(parent, &continued),
        success_do_nothing(),
        error_test_fail()
    );

    ProcessChildEvent target = ProcessChildEvent__new();
    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, child->pid,
                                       PROCESS_CHILD_WAIT_NOHANG, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_PENDING, result);
        },
        error_test_fail()
    );
    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, child->pid,
                                       PROCESS_CHILD_WAIT_UNTRACED, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_STOPPED, target.type);
        },
        error_test_fail()
    );
    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, child->pid,
                                       PROCESS_CHILD_WAIT_CONTINUED, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_CONTINUED, target.type);
        },
        error_test_fail()
    );
}

void test_process_wait_child_event_nohang_distinguishes_live_and_missing_children(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr child = dummy_process(42, cpu, PROC_STATE_READY);
    child->parent = Some(ProcessPtr, parent);
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    Scheduler__schedule(kernel->scheduler, child);
    ProcessChildEvent target = ProcessChildEvent__new();

    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, child->pid,
                                       PROCESS_CHILD_WAIT_NOHANG, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_PENDING, result);
            TEST_ASSERT_EQUAL(PROC_STATE_READY, parent->state);
        },
        error_test_fail()
    );

    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, 99,
                                       PROCESS_CHILD_WAIT_NOHANG, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_NO_CHILD, code);
        }
    );
}

void test_selected_child_wait_ignores_unrelated_child_exit(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr selected = dummy_process(42, cpu, PROC_STATE_READY);
    ProcessPtr other = dummy_process(43, cpu, PROC_STATE_READY);
    selected->parent = Some(ProcessPtr, parent);
    other->parent = Some(ProcessPtr, parent);
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, selected, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, other, cpu);
    ProcessChildEvent target = ProcessChildEvent__new();

    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, selected->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_QUEUE_EMPTY, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);

    match_result(Scheduler__exit(kernel->scheduler, other, 3),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);
    TEST_ASSERT_TRUE(Process__has_parked_operation(parent, PARK_WAIT_CHILD_EVENT, &Some(OperationKey, selected->pid)));

    match_result(Scheduler__exit(kernel->scheduler, selected, 4),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parent->state);
}

void test_child_exit_events_use_posix_wait_status_encoding(void)
{
    ProcessPtr parent = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr exited = dummy_process(42, cpu, PROC_STATE_READY);
    ProcessPtr signaled = dummy_process(43, cpu, PROC_STATE_READY);
    ProcessID exited_pid = exited->pid;
    ProcessID signaled_pid = signaled->pid;
    exited->parent = Some(ProcessPtr, parent);
    signaled->parent = Some(ProcessPtr, parent);
    Scheduler__schedule_on_cpu(kernel->scheduler, parent, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, exited, cpu);
    Scheduler__schedule_on_cpu(kernel->scheduler, signaled, cpu);
    CPUUsage__start_mode(&exited->cpu_usage, 0, 0, CPU_USAGE_USER);
    CPUUsage__transition(&exited->cpu_usage, 10, 0, CPU_USAGE_SYSTEM);
    test_cpu_time_usec = 20;

    match_result(Scheduler__exit(kernel->scheduler, exited, 7),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__exit(kernel->scheduler, signaled, PROCESS_SIGNAL_EXIT_BASE - PROCESS_SIGNAL_KILL),
                 success_do_nothing(),
        error_test_fail()
    );

    ProcessChildEvent target = ProcessChildEvent__new();
    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, exited_pid, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_WAIT_STATUS_EXITED(7), target.status);
            TEST_ASSERT_EQUAL_UINT64(10, parent->children_user_usec);
            TEST_ASSERT_EQUAL_UINT64(10, parent->children_system_usec);
        },
        error_test_fail()
    );
    match_result(
        ProcessWaitChildEvent__handler(kernel, Some(ProcessPtr, parent), (Anything)&target, signaled_pid, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_OK, result);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_WAIT_STATUS_SIGNALED(PROCESS_SIGNAL_KILL), target.status);
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

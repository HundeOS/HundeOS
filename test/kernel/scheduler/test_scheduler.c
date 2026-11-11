#include "unity.h"
#include "../stubs/panic.h"
#include "../stubs/cpu.h"
#include "../stubs/cpu_timer.h"
#include "../stubs/process.h"
#include "../stubs/mmu.h"
#include "../stubs/kalloc.h"
#include "../stubs/thread_context_layout.h"
#include "../fixtures/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/cpu_timer.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/service/service.h"
#include "../../../kernel/service/service_directory.h"
#include "../fixtures/kernel.h"

#include <string.h>

static SchedulerPtr scheduler = NULL;
static KernelPtr kernel = NULL;
static ParkedOperationPtr continuation_context;
static KernelContextPtr continuation_kernel_context;
static ProcessPtr expected_active_process = NULL;
static bool continuation_saw_active_address_space = false;
static bool remote_exit_saw_live_process = false;

static const CPUID cpu_id = 12;
extern uint64_t test_cpu_time_usec;
void BasicIO__reset_output(void);
const char * BasicIO__output(void);

void Arch__reset_irq_tracking();
uint64_t Arch__masked_irq();
uint64_t Arch__unmasked_irq();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();

static BString no_return_code = bstr("no return code");
static struct ThreadContext saved_context;

static ThreadContextPtr test_saved_context(void)
{
    saved_context.elr = 0xFFEE00FF;
    return &saved_context;
}

static void test_timer_interrupt(IRQNumber irq, CPUID cpu, ThreadContextPtr context, bool current_privilege)
{
    IRQDispatchContext irq_context = IRQDispatchContext__new(irq, cpu, current_privilege, context);
    match_result(
        CPUTimer__handle_irq(&irq_context, kernel),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_HANDLED, status);
        },
        error_test_fail()
    );
}

static KernelContext test_empty_kernel_context(void)
{
    return KernelContext__new(&None(ThreadPtr), &None(SchedulerPtr));
}

static KernelContext test_scheduler_kernel_context(void)
{
    return KernelContext__new(&None(ThreadPtr), &Some(SchedulerPtr, scheduler));
}

static KernelContextPtr test_empty_kernel_context_heap(void)
{
    return KernelContext__heap_new(&None(ThreadPtr), &None(SchedulerPtr));
}

static ParkedOperationPtr test_wait_io_operation(void)
{
    return ParkedOperation__heap_new_with_context_ownership(test_empty_kernel_context_heap(), &None(Continuation),
                                                            PARK_WAIT_IO, true);
}

static Result(ServiceEvent) test_service_wait(ProcessPtr service_proc, KernelContextPtr kernel_context)
{
    return Scheduler__service_wait(scheduler, service_proc, kernel_context, &None(Continuation));
}

void setUp(void)
{
    test_cpu_time_usec = 0;
    continuation_context = NULL;
    continuation_kernel_context = NULL;
    expected_active_process = NULL;
    continuation_saw_active_address_space = false;
    remote_exit_saw_live_process = false;
    clear_panic_state();
    set_cpu_id(cpu_id);
    reset_cpu_count();
    CPUStub__set_reschedule_hook(NULL);
    current_process_reset();
    reset_mmu_state();
    clear_deferred_syscall_result();
    Arch__reset_irq_tracking();
    CPUTimerStub__reset();
    kernel = init_test_kernel();
    scheduler = kernel->scheduler;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
    scheduler = NULL;
}

Result(Anything) continuation(ParkedOperationPtr parked_operation)
{
    continuation_context = parked_operation;
    continuation_kernel_context = parked_operation->kernel_context;
    continuation_saw_active_address_space = active_address_space_process() == expected_active_process;

    match_optional(parked_operation->return_code,
        some(code)
        {
            return Success(Anything, code);
        },
        none_do_nothing()
    );

    return Error(Anything, no_return_code, -1);
}

void test_context_switch_accounts_outgoing_and_incoming_processes(void)
{
    ProcessPtr first = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr second = dummy_process(2, cpu_id, PROC_STATE_READY);
    ContextSwitch initial = ContextSwitch__new(&first->thread, &None(ThreadPtr));
    ContextSwitch__switch(&initial);
    test_cpu_time_usec = 25000;
    ContextSwitch next = ContextSwitch__new(&second->thread, &Some(ThreadPtr, &first->thread));
    ContextSwitch__switch(&next);
    test_cpu_time_usec = 50000;
    ProcessCPUUsage first_usage = CPUUsage__snapshot(&first->accounting.cpu_usage, test_cpu_time_usec, 0);
    ProcessCPUUsage second_usage = CPUUsage__snapshot(&second->accounting.cpu_usage, test_cpu_time_usec, 0);
    TEST_ASSERT_EQUAL_UINT64(25000, first_usage.total_usec);
    TEST_ASSERT_EQUAL_UINT64(25000, second_usage.total_usec);
    TEST_ASSERT_EQUAL_UINT64(2500, first_usage.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(2500, second_usage.short_basis_points);
}

void test_context_switch_records_queue_wake_to_selection_delay_once(void)
{
    ProcessPtr process = dummy_process(1, cpu_id, PROC_STATE_READY);
    QueueWaitUsage__park(&process->thread.queue_wait_usage);
    test_cpu_time_usec = 20000;
    QueueWaitUsage__wake(&process->thread.queue_wait_usage, QUEUE_WAIT_TIMEOUT);
    test_cpu_time_usec = 27000;
    ContextSwitch next = ContextSwitch__new(&process->thread, &None(ThreadPtr));
    ContextSwitch__switch(&next);
    test_cpu_time_usec = 40000;
    ContextSwitch__switch(&next);
    ProcessQueueWaitUsage usage = QueueWaitUsage__snapshot(&process->thread.queue_wait_usage);
    TEST_ASSERT_EQUAL_UINT64(1, usage.selections);
    TEST_ASSERT_EQUAL_UINT64(7000, usage.runnable_usec);
    TEST_ASSERT_EQUAL_UINT64(7000, usage.max_runnable_usec);
}

void test_parking_stops_cpu_usage_before_outgoing_process_selection(void)
{
    ProcessPtr process = dummy_process(1, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    ContextSwitch initial = ContextSwitch__new(&process->thread, &None(ThreadPtr));
    ContextSwitch__switch(&initial);
    test_cpu_time_usec = 25000;
    KernelContext context = test_scheduler_kernel_context();
    match_result(Scheduler__park(scheduler, process, &context, &None(Continuation), PARK_WAIT_CHILD),
                 success_do_nothing(),
        error_test_fail()
    );
    test_cpu_time_usec = 2000000;
    ProcessCPUUsage snapshot = CPUUsage__snapshot(&process->accounting.cpu_usage, test_cpu_time_usec, 0);
    TEST_ASSERT_EQUAL_UINT64(25000, snapshot.total_usec);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.short_basis_points);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.long_basis_points);
}

void test_process_snapshot_reports_assigned_cpu_on_multiple_schedulers(void)
{
    ProcessPtr first = dummy_process(1, 0, PROC_STATE_READY);
    ProcessPtr second = dummy_process(2, cpu_id, PROC_STATE_WAITING);
    match_result(Scheduler__schedule_on_cpu(scheduler, first, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, second, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    ProcessInfo entries[4];
    ProcessListResult count = Scheduler__process_info_snapshot(scheduler, entries, 4);
    bool found_first = false;
    bool found_second = false;
    for (ProcessListResult index = 0; index < count; index++)
    {
        if (entries[index].pid == first->pid)
        {
            found_first = true;
            TEST_ASSERT_EQUAL_UINT64(0, entries[index].cpu);
        }
        if (entries[index].pid == second->pid)
        {
            found_second = true;
            TEST_ASSERT_EQUAL_UINT64(cpu_id, entries[index].cpu);
        }
    }
    TEST_ASSERT_TRUE(found_first);
    TEST_ASSERT_TRUE(found_second);
}

void test_kernel_context_reports_no_state_when_empty()
{
    KernelContext context = test_empty_kernel_context();

    TEST_ASSERT_FALSE(KernelContext__has_any_state(&context));
}

void test_kernel_context_reports_state_when_field_is_present()
{
    Thread thread = {0};
    KernelContext context = KernelContext__new(&Some(ThreadPtr, &thread), &None(SchedulerPtr));

    TEST_ASSERT_TRUE(KernelContext__has_any_state(&context));
}

void test_parked_operation_is_ready_without_blocked_operations()
{
    ParkedOperationPtr operation = test_wait_io_operation();
    ParkedOperation__mark_ready(operation);

    TEST_ASSERT_TRUE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(0, operation->pending_operations);

    ParkedOperation__dispose(operation);
}

void test_parked_operation_waits_until_all_blocked_operations_resolve_out_of_order()
{
    ParkedOperationPtr operation = test_wait_io_operation();
    ParkedOperation__mark_ready(operation);
    ParkedOperationPtr first_dependency = test_wait_io_operation();
    ParkedOperationPtr second_dependency = test_wait_io_operation();

    match_result(Scheduler__add_blocked_operation(scheduler, operation, first_dependency),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Scheduler__add_blocked_operation(scheduler, operation, second_dependency),
        success_do_nothing(),
                 error_test_fail()
    );

    TEST_ASSERT_FALSE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(2, operation->pending_operations);

    match_result(Scheduler__resolve_blocked_operation(scheduler, operation, second_dependency),
        success_do_nothing(),
                 error_test_fail()
    );

    TEST_ASSERT_FALSE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(1, operation->pending_operations);
    TEST_ASSERT_EQUAL_UINT64(1, BlockedParkedOperations__len(&operation->blocked_operations));

    match_result(Scheduler__resolve_blocked_operation(scheduler, operation, first_dependency),
        success_do_nothing(),
                 error_test_fail()
    );

    TEST_ASSERT_TRUE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(0, operation->pending_operations);
    TEST_ASSERT_EQUAL_UINT64(0, BlockedParkedOperations__len(&operation->blocked_operations));

    ParkedOperation__dispose(operation);
}

void test_parked_operation_disposes_unresolved_blocked_operations()
{
    ParkedOperationPtr operation = test_wait_io_operation();
    ParkedOperation__mark_ready(operation);
    ParkedOperationPtr blocked_operation = test_wait_io_operation();

    match_result(ParkedOperation__add_blocked_operation(operation, blocked_operation),
        success_do_nothing(),
                 error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(1, operation->pending_operations);
    TEST_ASSERT_EQUAL_UINT64(1, BlockedParkedOperations__len(&operation->blocked_operations));

    ParkedOperation__dispose(operation);
}

void test_scheduler_interrupt_when_no_processes_are_running_stays_in_idle(void)
{
    ThreadContextPtr context = test_saved_context();
    Scheduler__start(scheduler, cpu_id);

    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));
    ProcessPtr idle = unwrap_optional(Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});
    ProcessPtr current = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});

    TEST_ASSERT_EQUAL(idle, current);
    TEST_ASSERT_EQUAL(context->elr, Process__required_context(idle)->elr);
    TEST_ASSERT_EQUAL(idle->thread.state, PROC_STATE_RUNNING);
    TEST_ASSERT_EQUAL(current_running_process(), idle);
}

void test_scheduler_interrupt_before_cpu_start_does_not_context_switch(void)
{
    CPUID unstarted_cpu = 1;
    ThreadContextPtr context = test_saved_context();
    ProcessPtr idle = unwrap_optional(Scheduler__idle_process(scheduler, unstarted_cpu), (ProcessPtr){});

    current_process_reset();

    Scheduler__preempt(scheduler, unstarted_cpu, &Some(ThreadContextPtr, context));

    TEST_ASSERT_NULL(current_running_process());
    TEST_ASSERT_NOT_EQUAL(PROC_STATE_RUNNING, idle->thread.state);
}

void test_scheduler_init_creates_cpu_slots_independent_of_online_cpu_count(void)
{
    set_cpu_count(MAX_CPUS);

    SchedulerPtr local_scheduler = Scheduler__heap_new();
    Elf idle = unwrap_optional(EmbeddedElf__load(&bstr("hello_world")), (Elf){});
    SystemInfo system_info = (SystemInfo){
        .cpu_count = 1,
    };
    kernel->system_info = Some(SystemInfo, system_info);

    match_result(Scheduler__init(kernel, local_scheduler, idle),
        success_do_nothing(),
        error_test_fail()
    );

    match_optional(Scheduler__idle_process(local_scheduler, 0),
        some_test_fail(),
        none_do_nothing()
    );

    CPUID sparse_cpu = 37;
    match_result(Scheduler__register_cpu(local_scheduler, sparse_cpu),
        success_do_nothing(),
        error_test_fail()
    );

    match_optional(
        Scheduler__idle_process(local_scheduler, sparse_cpu),
        some(idle_process)
        {
            TEST_ASSERT_NOT_NULL(idle_process);
        },
        none_test_fail()
    );

    match_optional(Scheduler__idle_process(local_scheduler, 1),
        some_test_fail(),
        none_do_nothing()
    );

    reset_cpu_count();
}

void test_scheduler_requires_capability_from_live_channel_client(void)
{
    ProcessPtr client = dummy_process(1, cpu_id, PROC_STATE_READY);
    Process__set_identity(client, PROCESS_USER_HUND, PROCESS_GROUP_HUND, PROCESS_CAP_NONE);
    Scheduler__schedule(scheduler, client);

    match_result(
        Scheduler__require_process_capability(scheduler, client->pid, PROCESS_CAP_SUBSCRIBE_IRQ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
    );

    Process__set_identity(client, PROCESS_USER_HUND, PROCESS_GROUP_HUND, PROCESS_CAP_SUBSCRIBE_IRQ);
    match_result(
        Scheduler__require_process_capability(scheduler, client->pid, PROCESS_CAP_SUBSCRIBE_IRQ),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );
}

void test_all_processes_when_processes_are_scheduled()
{
    ProcessPtr proc1 = dummy_process(1, 1, PROC_STATE_READY);
    ProcessPtr proc2 = dummy_process(2, 1, PROC_STATE_READY);
    ProcessPtr proc3 = dummy_process(3, 1, PROC_STATE_READY);

    Scheduler__start(scheduler, 1);
    Scheduler__schedule_on_cpu(scheduler, proc1, 1);
    Scheduler__schedule_on_cpu(scheduler, proc2, 1);
    Scheduler__schedule_on_cpu(scheduler, proc3, 1);

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, 1);

    TEST_ASSERT_EQUAL(3, ProcessTable__len(processes));
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, 1), (CPUSchedulerPtr){});
    TEST_ASSERT_EQUAL(3, ThreadTable__len(&cpu->threads));
    match_optional(ThreadTable__find_item(&cpu->threads, &proc2->thread),
        some_do_nothing(),
        none_test_fail()
    );
    match_optional(ProcessTable__find_item(processes, proc1),
        some_do_nothing(),
        none_test_fail()
    );
    match_optional(ProcessTable__find_item(processes, proc2),
        some_do_nothing(),
        none_test_fail()
    );
    match_optional(ProcessTable__find_item(processes, proc3),
        some_do_nothing(),
        none_test_fail()
    );
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

void test_scheduler_save_context_to_current_thread(void)
{
    ThreadContextPtr context = test_saved_context();
    Scheduler__start(scheduler, cpu_id);

    match_optional(
        Scheduler__save_context(scheduler, cpu_id, &Some(ThreadContextPtr, context)),
        some(thread)
        {
            TEST_ASSERT_EQUAL(context->elr, Thread__required_context(thread)->elr);
            TEST_ASSERT_EQUAL_PTR(thread, &thread->owner->thread);

            match_optional(
                Scheduler__current_process(scheduler, cpu_id),
                some(current)
                {
                    TEST_ASSERT_EQUAL(context->elr, Process__required_context(current)->elr);
                },
                none_test_fail()
            );
        },
        none_test_fail()
    );
}

void test_scheduler_saves_running_sibling_instead_of_process_initial_thread(void)
{
    ProcessPtr process = dummy_process(1, cpu_id, PROC_STATE_READY);
    Scheduler__start(scheduler, cpu_id);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, cpu_id), (CPUSchedulerPtr){});
    ThreadContextPtr initial_context = Process__required_context(process);
    uint64_t initial_elr = initial_context->elr;
    ThreadContext sibling_context = {.elr = 0x1234};
    ThreadContext source = {.elr = 0x5678};
    Thread sibling = Thread__new(process, &process->thread.stack);
    sibling.context = Some(ThreadContextPtr, &sibling_context);
    sibling.state = PROC_STATE_RUNNING;
    match_result(ThreadTable__push(&cpu->threads, &sibling),
        success_do_nothing(),
        error_test_fail()
    );

    match_optional(
        Scheduler__save_context(scheduler, cpu_id, &Some(ThreadContextPtr, &source)),
        some(thread)
        {
            TEST_ASSERT_EQUAL_PTR(&sibling, thread);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(source.elr, sibling_context.elr);
    TEST_ASSERT_EQUAL_UINT64(initial_elr, initial_context->elr);
}

void test_scheduler_save_context_returns_none_when_context_save_fails(void)
{
    Scheduler__start(scheduler, cpu_id);
    ProcessPtr current = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    Optional(ThreadContextPtr) original_context = current->thread.context;
    current->thread.context = None(ThreadContextPtr);

    match_optional(Scheduler__save_context(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context())),
                   some_test_fail(),
        none_do_nothing()
    );

    current->thread.context = original_context;
}

void test_scheduler_interrupt_does_not_switch_when_context_save_fails(void)
{
    ProcessPtr ready = dummy_process(0, cpu_id, PROC_STATE_READY);
    Scheduler__schedule_on_cpu(scheduler, ready, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    ProcessPtr current = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    Tick original_last_tick = current->thread.last_tick;
    Optional(ThreadContextPtr) original_context = current->thread.context;
    current->thread.context = None(ThreadContextPtr);

    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));

    TEST_ASSERT_EQUAL_PTR(current, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->thread.state);
    TEST_ASSERT_EQUAL_UINT64(original_last_tick, current->thread.last_tick);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, ready->thread.state);

    current->thread.context = original_context;
}

void test_signal_request_does_not_switch_or_remove_remote_process(void)
{
    CPUID remote_cpu = 1;
    ProcessPtr target = dummy_process(100, remote_cpu, PROC_STATE_READY);
    ProcessID pid = target->pid;
    match_result(Scheduler__schedule_on_cpu(scheduler, target, remote_cpu),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__start(scheduler, remote_cpu);
    Scheduler__start(scheduler, cpu_id);
    ProcessPtr caller = current_running_process();

    match_result(
        Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_KILL),
        success(signalled)
        {
            TEST_ASSERT_EQUAL(pid, signalled);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_PTR(caller, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_READY, target->thread.state);
    TEST_ASSERT_TRUE(Process__has_pending_signal(target));
    match_optional(
        Scheduler__find_process_by_pid(scheduler, pid),
        some(found)
        {
            TEST_ASSERT_EQUAL_PTR(target, found);
        },
        none_test_fail()
    );

    set_cpu_id(remote_cpu);
    Scheduler__preempt(scheduler, remote_cpu, &Some(ThreadContextPtr, test_saved_context()));
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_NOT_EQUAL(target, current_running_process());
}

void test_cpu_timer_interval_uses_requested_frequency(void)
{
    TEST_ASSERT_EQUAL_UINT64(10000, CPUTimer__interval(10000000, 1000));
}

void test_cpu_timer_interval_defaults_to_1000_hz(void)
{
    TEST_ASSERT_EQUAL_UINT64(10000, CPUTimer__interval(10000000, 0));
}

void test_cpu_timer_interval_is_at_least_one_tick(void)
{
    TEST_ASSERT_EQUAL_UINT64(1, CPUTimer__interval(100, 1000));
}

void test_timer_terminates_running_process_without_another_syscall(void)
{
    IRQNumber irq = 27;
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = target->pid;
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));
    TEST_ASSERT_EQUAL_PTR(target, current_running_process());

    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_TERMINATE),
        success_do_nothing(),
                 error_test_fail()
    );
    test_timer_interrupt(irq, cpu_id, test_saved_context(), false);

    TEST_ASSERT_EQUAL_UINT64(1, CPUTimerStub__rearm_count());
    TEST_ASSERT_EQUAL_UINT64(irq, CPUTimerStub__last_irq());
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_NOT_EQUAL(target, current_running_process());
}

void test_kernel_timer_tick_does_not_preempt_interrupted_process(void)
{
    IRQNumber irq = 27;
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = target->pid;
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));

    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_TERMINATE),
        success_do_nothing(),
                 error_test_fail()
    );

    test_timer_interrupt(irq, cpu_id, test_saved_context(), true);

    TEST_ASSERT_EQUAL_UINT64(1, CPUTimerStub__rearm_count());
    TEST_ASSERT_EQUAL_UINT64(irq, CPUTimerStub__last_irq());
    TEST_ASSERT_EQUAL_PTR(target, current_running_process());
    TEST_ASSERT_TRUE(Process__has_pending_signal(target));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, target->thread.state);
}

void test_signal_selects_blocked_process_without_running_its_continuation(void)
{
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = target->pid;
    KernelContext kernel_context = test_scheduler_kernel_context();
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    match_result(Scheduler__park(scheduler, target, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
                 success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, target->thread.state);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));
    TEST_ASSERT_NULL(continuation_context);
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_signal_precedes_ready_deferred_syscall(void)
{
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = target->pid;
    KernelContext kernel_context = test_scheduler_kernel_context();
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    match_result(Scheduler__park(scheduler, target, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
                 success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__unpark(scheduler, target, PARK_WAIT_IO, &Some(ReturnCode, 11)),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));
    TEST_ASSERT_NULL(continuation_context);
    TEST_ASSERT_NULL(active_address_space_process());
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_signal_terminates_sleeping_process_before_deadline(void)
{
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = target->pid;
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    match_result(Scheduler__sleep_usec(scheduler, &target->thread, 100000000),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, target->thread.state);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_signal_request_during_selected_context_switch_is_delivered_before_resume(void)
{
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = target->pid;
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Optional(ProcessPtr) old = Scheduler__current_process(scheduler, cpu_id);
    match_optional(old,
        some(process)
        {
            process->thread.state = PROC_STATE_SWITCHING_CONTEXT;
        },
        none_test_fail()
    );
    target->thread.state = PROC_STATE_SWITCHING_CONTEXT;
    Optional(ThreadPtr) old_thread = None(ThreadPtr);
    match_optional(old,
        some(process)
        {
            old_thread = Some(ThreadPtr, &process->thread);
        },
        none_do_nothing()
    );
    ContextSwitch selected = ContextSwitch__new(&target->thread, &old_thread);
    TEST_ASSERT_EQUAL(PROC_STATE_SWITCHING_CONTEXT, target->thread.state);
    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_SWITCHING_CONTEXT, target->thread.state);
    ContextSwitch__switch(&selected);
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_NOT_EQUAL(target, current_running_process());
}

void test_scheduler_exit_detaches_inactive_sibling(void)
{
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, proc, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Thread sibling = Thread__new(proc, &proc->thread.stack);
    ThreadContextPtr sibling_context = ThreadContext__clone(kernel, Process__required_context(proc));
    sibling.context = Some(ThreadContextPtr, sibling_context);
    match_result(ProcessThreads__push(&proc->threads, &sibling),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(Scheduler__exit(scheduler, proc, 10),
        success_do_nothing(),
        error_test_fail()
    );
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, cpu_id), (CPUSchedulerPtr){});
    TEST_ASSERT_EQUAL(0, ProcessTable__len(&cpu->processes));
    TEST_ASSERT_EQUAL(0, ThreadTable__len(&cpu->threads));
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, sibling.state);
    TEST_ASSERT_FALSE(KAllocStub__is_allocated((Address)sibling_context));
}

static void acknowledge_remote_exit(CPUID remote_cpu)
{
    if (remote_cpu != 1)
    {
        return;
    }
    remote_exit_saw_live_process = Scheduler__process_count(scheduler) == 1 && MMUStub__dereference_count() == 0;
    ProcessPtr idle = unwrap_optional(Scheduler__idle_process(scheduler, remote_cpu), (ProcessPtr){});
    Scheduler__address_space_switched(scheduler, remote_cpu, idle);
}

void test_scheduler_exit_waits_for_remote_address_space_switch(void)
{
    CPUID remote_cpu = 1;
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    KernelAddress page_table = ProcessPageTable__root(&proc->address_space.page_table);
    match_result(Scheduler__schedule_on_cpu(scheduler, proc, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__address_space_switched(scheduler, remote_cpu, proc);
    CPUStub__set_reschedule_hook(acknowledge_remote_exit);

    match_result(Scheduler__exit(scheduler, proc, 10),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_TRUE(remote_exit_saw_live_process);
    TEST_ASSERT_EQUAL(remote_cpu, CPUStub__rescheduled_cpu());
    TEST_ASSERT_FALSE(KAllocStub__is_allocated(page_table.virtual_addr));
}

void test_scheduler_exit_does_not_reclaim_an_already_terminating_process(void)
{
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, proc, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    proc->lifecycle.terminating = true;

    match_result(
        Scheduler__exit(scheduler, proc, 10),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SCHEDULER_GENERAL_ERROR, code);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(0, MMUStub__dereference_count());
    match_optional(
        Scheduler__find_process_by_pid(scheduler, proc->pid),
        some(found)
        {
            TEST_ASSERT_EQUAL(proc, found);
        },
        none_test_fail()
    );
}

void test_scheduler_exit_removes_process_when_its_current(void)
{
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessID pid = proc->pid;
    ThreadContextPtr context = test_saved_context();
    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    ProcessPtr idle = unwrap_optional(Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});

    match_result(
        Scheduler__exit(scheduler, proc, 10),
        success(exited_pid)
        {
            TEST_ASSERT_EQUAL(pid, exited_pid);
        },
        error_test_fail()
    );

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(idle, current);
        },
        none_test_fail()
    );

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(0, ProcessTable__len(processes));
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, cpu_id), (CPUSchedulerPtr){});
    TEST_ASSERT_EQUAL(0, ThreadTable__len(&cpu->threads));
    TEST_ASSERT_EQUAL_UINT64(1, MMUStub__dereference_count());
    TEST_ASSERT_TRUE(MMUStub__dereferenced_before_free());
    TEST_ASSERT_EQUAL_HEX64(ProcessPageTable__root(&idle->address_space.page_table).phys_addr,
                            MMUStub__active_table().phys_addr);
}

void test_scheduler_exit_orphans_live_children_on_other_cpus(void)
{
    CPUID remote_cpu = 1;
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr local_child = dummy_process_with_parent(2, cpu_id, PROC_STATE_READY, parent);
    ProcessPtr remote_child = dummy_process_with_parent(3, remote_cpu, PROC_STATE_READY, parent);
    ProcessID local_pid = local_child->pid;

    match_result(Scheduler__schedule_on_cpu(scheduler, parent, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, local_child, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, remote_child, remote_cpu),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(2, LiveChildren__len(&parent->live_children));

    match_result(Scheduler__exit(scheduler, parent, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(local_child->parent));
    TEST_ASSERT_TRUE(is_nothing(local_child->parent_pid));
    TEST_ASSERT_TRUE(is_nothing(remote_child->parent));
    TEST_ASSERT_TRUE(is_nothing(remote_child->parent_pid));

    match_result(
        Scheduler__exit(scheduler, local_child, 0),
        success(exited_pid)
        {
            TEST_ASSERT_EQUAL(local_pid, exited_pid);
        },
        error_test_fail()
    );
}

void test_scheduler_removes_exited_child_from_parent_live_children(void)
{
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr first = dummy_process_with_parent(2, cpu_id, PROC_STATE_READY, parent);
    ProcessPtr second = dummy_process_with_parent(3, cpu_id, PROC_STATE_READY, parent);

    match_result(Scheduler__schedule_on_cpu(scheduler, parent, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, first, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, second, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(2, LiveChildren__len(&parent->live_children));

    match_result(Scheduler__exit(scheduler, first, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, LiveChildren__len(&parent->live_children));
    TEST_ASSERT_EQUAL_PTR(second, unwrap_optional(LiveChildren__at(&parent->live_children, 0), (ProcessPtr){}));

    match_result(Scheduler__exit(scheduler, parent, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(second->parent));
    TEST_ASSERT_TRUE(is_nothing(second->parent_pid));
}

void test_scheduler_orphans_child_if_parent_exits_before_publication(void)
{
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process_with_parent(2, cpu_id, PROC_STATE_READY, parent);

    match_result(Scheduler__schedule_on_cpu(scheduler, parent, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__exit(scheduler, parent, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, child, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(child->parent));
    TEST_ASSERT_TRUE(is_nothing(child->parent_pid));
}

void test_scheduler_exit_unparks_parent_if_waiting(void)
{
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process_with_parent(1, cpu_id, PROC_STATE_READY, parent);
    ProcessID child_pid = child->pid;

    KernelContext kernel_context = test_scheduler_kernel_context();

    ThreadContextPtr context = test_saved_context();
    Scheduler__schedule(scheduler, parent);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));
    Scheduler__schedule(scheduler, child);
    match_result(
        Scheduler__park(scheduler, parent, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_CHILD),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(child, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->thread.state);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__exit(scheduler, child, 10),
        success(exited_pid)
        {
            TEST_ASSERT_EQUAL(child_pid, exited_pid);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->thread.state);

    TEST_ASSERT_EQUAL(10, Thread__required_context(&parent->thread)->x[0]);

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(parent, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->thread.state);
        },
        none_test_fail()
    );
}

void test_scheduler_abort_process_records_fault_and_unparks_parent(void)
{
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process_with_parent(2, cpu_id, PROC_STATE_READY, parent);
    ProcessID child_pid = child->pid;
    BString fault_prefix_format = bstr("process fault: pid=%i name=%s");
    BString fault_prefix = BString__sprintf(&fault_prefix_format, child_pid, cstr(child->name));

    KernelContext kernel_context = test_scheduler_kernel_context();
    SpawnAbortReport abort_report = SpawnAbortReport__none();
    KernelAddress abort_report_address = KERNEL_ADDR((Address)&abort_report, 0);

    ThreadContextPtr context = test_saved_context();
    Scheduler__schedule(scheduler, parent);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));
    Scheduler__schedule(scheduler, child);
    match_result(Scheduler__park_wait_child(scheduler, parent, &kernel_context, &Some(Continuation, continuation),
                                            &Some(KernelAddress, abort_report_address), false),
                 success_do_nothing(),
        error_test_fail()
    );
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    ProcessAbort abort = (ProcessAbort){
        .reason = PROCESS_ABORT_DATA_ABORT,
        .pc = 0x1234567800400100ULL,
        .fault_address = 0xabcdef0100000000ULL,
        .exception_code = 0x200000024ULL,
        .syndrome = 0x196000004ULL,
        .cpu = cpu_id,
    };

    BasicIO__reset_output();
    match_result(
        Scheduler__abort_process(scheduler, child, abort),
        success(aborted_pid)
        {
            TEST_ASSERT_EQUAL(child_pid, aborted_pid);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_INT64(PROCESS_ABORT_EXIT_BASE - PROCESS_ABORT_DATA_ABORT,
                            (int64_t)Thread__required_context(&parent->thread)->x[0]);
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), BString__cstr(&fault_prefix)));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "reason=data abort"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "pc=0x1234567800400100"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "far=0xabcdef0100000000"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "esr=0x196000004"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "ec=0x200000024"));
    TEST_ASSERT_TRUE(SpawnAbortReport__is_present(abort_report));
    TEST_ASSERT_EQUAL_UINT64(PROCESS_ABORT_DATA_ABORT, abort_report.reason);
    TEST_ASSERT_EQUAL_HEX64(0x200000024ULL, abort_report.exception_code);
    TEST_ASSERT_EQUAL_HEX64(0x1234567800400100ULL, abort_report.pc);
    TEST_ASSERT_EQUAL_HEX64(0xabcdef0100000000ULL, abort_report.fault_address);
}

void test_scheduler_exit_removes_process_when_its_not_current(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = proc->pid;
    ProcessPtr other_proc = dummy_process(0, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();
    Scheduler__schedule_on_cpu(scheduler, proc, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, other_proc, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(current, other_proc);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__exit(scheduler, proc, 10),
        success(exited_pid)
        {
            TEST_ASSERT_EQUAL(pid, exited_pid);
        },
        error_test_fail()
    );

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(other_proc, current);
        },
        none_test_fail()
    );

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(1, ProcessTable__len(processes));
    TEST_ASSERT_EQUAL_UINT64(0, MMUStub__dereference_count());
    TEST_ASSERT_EQUAL_HEX64(ProcessPageTable__root(&other_proc->address_space.page_table).phys_addr,
                            MMUStub__active_table().phys_addr);
}

void test_scheduler_park_switches_process_to_waiting_and_switches_context(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    ProcessPtr idle = unwrap_optional(Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(current, proc);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__park(scheduler, proc, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->thread.state);
        },
        error_test_fail()
    );

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(idle, current);
        },
        none_test_fail()
    );

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(1, ProcessTable__len(processes));
}

void test_scheduler_unpark_switches_process_back_to_ready(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(current, proc);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__park(scheduler, proc, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->thread.state);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, &Some(ReturnCode, 11)),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->thread.state);
        },
        error_test_fail()
    );
}

void test_scheduler_sleep_usec_wakes_after_rounded_timer_ticks(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContext context = {0};

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));

    match_result(
        Scheduler__sleep_usec(scheduler, &proc->thread, 2500),
        success(sleeping)
        {
            TEST_ASSERT_EQUAL(proc, sleeping);
            TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, sleeping->thread.state);
        },
        error_test_fail()
    );

    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, proc->thread.state);

    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, proc->thread.state);

    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));
    TEST_ASSERT_EQUAL(proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->thread.state);
    TEST_ASSERT_EQUAL(PROCESS_SLEEP_OK, Thread__required_context(&proc->thread)->x[0]);
}

void test_scheduler_context_switch_finalises_ready_parked_operation_before_resume(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContext context = {0};

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));

    match_result(Scheduler__park(scheduler, proc, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
                 success_do_nothing(),
        error_test_fail()
    );

    match_result(Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, &Some(ReturnCode, 11)),
        success_do_nothing(),
                 error_test_fail()
    );

    expected_active_process = proc;
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));

    TEST_ASSERT_EQUAL(proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->thread.state);
    TEST_ASSERT_EQUAL(&kernel_context, continuation_kernel_context);
    TEST_ASSERT_TRUE(continuation_saw_active_address_space);
    TEST_ASSERT_EQUAL(11, Thread__required_context(&proc->thread)->x[0]);
}

void test_scheduler_park_binds_continuation_to_the_running_thread(void)
{
    ProcessPtr caller = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr other = dummy_process(200, cpu_id, PROC_STATE_READY);
    ThreadContext context = {0};
    KernelContext kernel_context = KernelContext__new(&Some(ThreadPtr, &other->thread), &Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, caller);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, &context));

    match_result(Scheduler__park(scheduler, caller, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
                 success_do_nothing(),
        error_test_fail()
    );

    match_optional(
        kernel_context.thread,
        some(thread)
        {
            TEST_ASSERT_EQUAL_PTR(&caller->thread, thread);
        },
        none_test_fail()
    );
}

void test_scheduler_park_rejects_a_different_process_thread_without_a_running_caller(void)
{
    ProcessPtr target = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr other = dummy_process(200, cpu_id, PROC_STATE_READY);
    KernelContext kernel_context = KernelContext__new(&Some(ThreadPtr, &other->thread), &Some(SchedulerPtr, scheduler));

    match_result(
        Scheduler__schedule_on_cpu(scheduler, target, cpu_id),
        success(scheduled_cpu)
        {
            TEST_ASSERT_EQUAL(cpu_id, scheduled_cpu);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__park(scheduler, target, &kernel_context, &None(Continuation), PARK_WAIT_IO),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SCHEDULER_GENERAL_ERROR, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_READY, target->thread.state);
}

void test_scheduler_unpark_gives_error_and_doesnt_unpark_if_reasons_dont_match(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(current, proc);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__park(scheduler, proc, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->thread.state);
        },
        error_test_fail()
    );

    match_result(Scheduler__unpark(scheduler, proc, PARK_WAIT_CHILD, &Some(ReturnCode, 11)),
        success_test_fail(),
                 error_test_ok()
    );
}

void test_scheduler_unpark_when_not_idle(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr running_proc = dummy_process(0, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule_on_cpu(scheduler, proc, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, running_proc, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(current, running_proc);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__park(scheduler, proc, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->thread.state);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, &Some(ReturnCode, 11)),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->thread.state);
        },
        error_test_fail()
    );

    // it should cause a context switch as CPU is currently idle
    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(running_proc, current);
        },
        none_test_fail()
    );
}

void test_scheduler_parks_sibling_without_process_record_on_its_cpu(void)
{
    CPUID remote_cpu = 1;
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Thread sibling = Thread__new(process, &process->thread.stack);
    sibling.state = PROC_STATE_READY;
    match_result(ProcessThreads__push(&process->threads, &sibling),
        success_do_nothing(),
        error_test_fail()
    );
    CPUSchedulerPtr remote =
        unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, remote_cpu), (CPUSchedulerPtr){});
    match_result(ThreadTable__push(&remote->threads, &sibling),
        success_do_nothing(),
        error_test_fail()
    );
    KernelContext context = KernelContext__new(&Some(ThreadPtr, &sibling), &Some(SchedulerPtr, scheduler));

    match_result(
        Scheduler__park(scheduler, process, &context, &None(Continuation), PARK_WAIT_IO),
        success(parked)
        {
            TEST_ASSERT_EQUAL(process, parked);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, sibling.state);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, process->thread.state);
}

void test_scheduler_creates_threads_with_kernel_owned_stacks_and_distinct_ids(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    ThreadID first_id = 0;
    ThreadID second_id = 0;
    match_result(
        Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0x1234, 0x4000),
        success(id)
        {
            first_id = id;
        },
        error_test_fail()
    );
    match_result(
        Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0x5678, 0x8000),
        success(id)
        {
            second_id = id;
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(2, first_id);
    TEST_ASSERT_EQUAL_UINT64(3, second_id);
    TEST_ASSERT_EQUAL_UINT64(3, ProcessThreads__len(&process->threads));
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, cpu_id), (CPUSchedulerPtr){});
    TEST_ASSERT_EQUAL_UINT64(3, ThreadTable__len(&cpu->threads));
    ThreadPtr first = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    ThreadPtr second = unwrap_optional(ProcessThreads__at(&process->threads, 2), (ThreadPtr){});
    TEST_ASSERT_NOT_EQUAL(process->thread.stack.base.virtual_addr, first->stack.base.virtual_addr);
    TEST_ASSERT_NOT_EQUAL(first->stack.base.virtual_addr, second->stack.base.virtual_addr);
    TEST_ASSERT_EQUAL_HEX64(entry, Thread__required_context(first)->elr);
    TEST_ASSERT_EQUAL_HEX64(entry, Thread__required_context(first)->x[0]);
    TEST_ASSERT_EQUAL_HEX64(0x1234, Thread__required_context(first)->x[1]);
    TEST_ASSERT_EQUAL_HEX64(0x4000, ThreadContext__thread_pointer(Thread__required_context(first)));
    TEST_ASSERT_EQUAL_HEX64(0x8000, ThreadContext__thread_pointer(Thread__required_context(second)));
}

void test_scheduler_rejects_invalid_thread_entry_without_publishing(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(
        Scheduler__create_thread(scheduler, &process->thread, 0, 0x1000, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_OPERATION_INVALID, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT64(1, ProcessThreads__len(&process->threads));
}

void test_scheduler_thread_stack_failure_does_not_publish_or_consume_id(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    KAllocStub__set_user_alloc_failure(true);
    Result(ThreadID) created = Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0);
    KAllocStub__set_user_alloc_failure(false);
    match_result(
        created,
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_OPERATION_NO_MEMORY, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT64(1, ProcessThreads__len(&process->threads));
    TEST_ASSERT_EQUAL_UINT64(2, process->next_thread_id);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, process->thread.state);
}

void test_scheduler_join_reaps_an_exited_thread_and_returns_its_value(void)
{
    KAllocStub__reset_allocation_tracking();
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    ThreadID id = unwrap_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0), 0);
    ThreadPtr target = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, cpu_id), (CPUSchedulerPtr){});
    match_result(ThreadTable__remove(&cpu->threads, target),
        success_do_nothing(),
        error_test_fail()
    );
    target->state = PROC_STATE_DEAD;
    target->exit_value = 0x1234;
    ThreadContextPtr saved_signal = ThreadContext__clone(kernel, Thread__required_context(target));
    match_result(ProcessSignalFrames__push(&target->signal_frames, ProcessSignalFrame__new(saved_signal, 0)),
                 success_do_nothing(),
        error_test_fail()
    );
    ParkedOperationPtr abandoned = test_wait_io_operation();
    Address abandoned_context = addr(abandoned->kernel_context);
    match_result(Thread__push_parked_operation(target, abandoned),
        success_do_nothing(),
        error_test_fail()
    );
    UserAddress output = unwrap_result(Process__alloc_user_pages(process, sizeof(ThreadJoinResult)), NULL_USER_ADDRESS);

    match_result(
        Scheduler__join_thread(scheduler, &process->thread, id, (ThreadJoinResultPtr)output.virtual_addr),
        success(code)
        {
            TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, code);
        },
        error_test_fail()
    );
    ThreadJoinResultPtr written = k_v_ptr(output, ThreadJoinResult);
    TEST_ASSERT_EQUAL_HEX64(0x1234, written->value);
    TEST_ASSERT_EQUAL_UINT64(1, ProcessThreads__len(&process->threads));
    TEST_ASSERT_EQUAL_UINT64(1, ThreadTable__len(&cpu->threads));
    TEST_ASSERT_FALSE(KAllocStub__is_allocated(addr(saved_signal)));
    TEST_ASSERT_FALSE(KAllocStub__is_allocated(addr(abandoned)));
    TEST_ASSERT_FALSE(KAllocStub__is_allocated(abandoned_context));
}

void test_scheduler_exiting_joiner_releases_its_target_before_reaping(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    ThreadID joiner_id = unwrap_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0), 0);
    ThreadID target_id = unwrap_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0), 0);
    ThreadPtr joiner = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    ThreadPtr target = unwrap_optional(ProcessThreads__at(&process->threads, 2), (ThreadPtr){});
    UserAddress output = unwrap_result(Process__alloc_user_pages(process, sizeof(ThreadJoinResult)), NULL_USER_ADDRESS);
    joiner->state = PROC_STATE_RUNNING;
    match_result(
        Scheduler__join_thread(scheduler, joiner, target_id, (ThreadJoinResultPtr)output.virtual_addr),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_IO_BLOCK, code);
        }
    );
    joiner->state = PROC_STATE_RUNNING;
    match_result(Scheduler__exit_thread(scheduler, joiner, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(target->joiner));
    match_result(
        Scheduler__join_thread(scheduler, &process->thread, joiner_id, (ThreadJoinResultPtr)output.virtual_addr),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_scheduler_join_rejects_invalid_result_before_parking(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    ThreadID id = unwrap_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0), 0);
    match_result(
        Scheduler__join_thread(scheduler, &process->thread, id, (ThreadJoinResultPtr)(uintptr_t)1),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_OPERATION_INVALID, code);
        }
    );
    TEST_ASSERT_FALSE(Thread__is_parked(&process->thread));
}

void test_scheduler_thread_exit_wakes_joiner_and_completes_join(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    ThreadID id = unwrap_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0), 0);
    ThreadPtr target = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    UserAddress output = unwrap_result(Process__alloc_user_pages(process, sizeof(ThreadJoinResult)), NULL_USER_ADDRESS);
    process->thread.state = PROC_STATE_RUNNING;
    match_result(
        Scheduler__join_thread(scheduler, &process->thread, id, (ThreadJoinResultPtr)output.virtual_addr),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_IO_BLOCK, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->thread.state);
    target->state = PROC_STATE_RUNNING;
    match_result(Scheduler__exit_thread(scheduler, target, 0x5678),
        success_do_nothing(),
        error_test_fail()
    );
    ThreadJoinResultPtr written = k_v_ptr(output, ThreadJoinResult);
    TEST_ASSERT_EQUAL_HEX64(0x5678, written->value);
    TEST_ASSERT_EQUAL_UINT64(1, ProcessThreads__len(&process->threads));
}

void test_scheduler_main_thread_exit_leaves_sibling_until_final_thread_exits(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = process->pid;
    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    Address entry = process->image.entry_point.virtual_addr;
    match_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0),
        success_do_nothing(),
                 error_test_fail()
    );
    ThreadPtr sibling = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    process->thread.state = PROC_STATE_RUNNING;
    match_result(Scheduler__exit_thread(scheduler, &process->thread, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, process->thread.state);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, sibling->state);
    TEST_ASSERT_TRUE(is_something(Scheduler__find_process_by_pid(scheduler, pid)));
    match_result(Scheduler__exit_thread(scheduler, sibling, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Scheduler__find_process_by_pid(scheduler, pid)));
}

void test_scheduler_unpark_by_key_wakes_only_matching_thread(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_WAITING);
    Stack stack = Stack__new(4096);
    Thread sibling = Thread__new(process, &stack);
    sibling.state = PROC_STATE_WAITING;
    KernelContext first_context =
        KernelContext__new(&Some(ThreadPtr, &process->thread), &Some(SchedulerPtr, scheduler));
    KernelContext sibling_context = KernelContext__new(&Some(ThreadPtr, &sibling), &Some(SchedulerPtr, scheduler));
    ParkedOperationPtr first = ParkedOperation__heap_new(&first_context, &None(Continuation), PARK_WAIT_IO);
    ParkedOperationPtr second = ParkedOperation__heap_new(&sibling_context, &None(Continuation), PARK_WAIT_IO);
    first->operation_key = Some(OperationKey, 10);
    second->operation_key = Some(OperationKey, 20);

    match_result(Scheduler__schedule_on_cpu(scheduler, process, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    process->thread.state = PROC_STATE_WAITING;
    CPUSchedulerPtr cpu = unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, cpu_id), (CPUSchedulerPtr){});
    match_result(ThreadTable__push(&cpu->threads, &sibling),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Thread__push_parked_operation(&process->thread, first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Thread__push_parked_operation(&sibling, second),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(Scheduler__unpark_by_pid_with_operation_key(scheduler, process->pid, PARK_WAIT_IO,
                                                             &Some(ReturnCode, 123), &Some(OperationKey, 20)),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->thread.state);
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, sibling.state);
    TEST_ASSERT_FALSE(Thread__has_ready_parked_operation(&process->thread, PARK_WAIT_IO, &None(OperationKey)));
    TEST_ASSERT_TRUE(Thread__has_ready_parked_operation(&sibling, PARK_WAIT_IO, &None(OperationKey)));

    match_optional(
        Thread__pop_ready_parked_operation(&sibling),
        some(operation)
        {
            TEST_ASSERT_EQUAL_PTR(second, operation);
            ParkedOperation__dispose(operation);
        },
        none_test_fail()
    );
    match_optional(
        Process__pop_parked_operation(process),
        some(operation)
        {
            TEST_ASSERT_EQUAL_PTR(first, operation);
            ParkedOperation__dispose(operation);
        },
        none_test_fail()
    );
    ThreadTable__remove(&cpu->threads, &sibling);
}

void test_scheduler_park_switches_process_to_waiting_not_switching_context_when_not_current(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr running = dummy_process(0, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    KernelContext kernel_context = (KernelContext){
        .scheduler = scheduler,
    };

    Scheduler__schedule_on_cpu(scheduler, proc, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, running, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(current, running);
        },
        none_test_fail()
    );

    match_result(
        Scheduler__park(scheduler, proc, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success(parked)
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->thread.state);
        },
        error_test_fail()
    );

    match_optional(
        Scheduler__current_process(scheduler, cpu_id),
        some(current)
        {
            TEST_ASSERT_EQUAL(running, current);
        },
        none_test_fail()
    );

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(2, ProcessTable__len(processes));
}

void test_scheduler_preempt_when_idle_with_a_process_ready(void)
{
    ThreadContextPtr context = test_saved_context();
    CPUID scheduled_cpu = 3;

    ProcessPtr dummy = dummy_process(0, scheduled_cpu, PROC_STATE_READY);

    match_result(
        Scheduler__schedule(scheduler, dummy),
        success(status)
        {
            TEST_ASSERT_EQUAL(SCHEDULER_SCHEDULE_OK, status);
        },
        error_test_fail()
    );

    ProcessInfo entries[8];
    ProcessListResult count = Scheduler__process_info_snapshot(scheduler, entries, 8);
    bool found_unassigned = false;
    for (ProcessListResult index = 0; index < count; index++)
    {
        if (entries[index].pid == dummy->pid)
        {
            found_unassigned = true;
            TEST_ASSERT_EQUAL_UINT64((CPUID)SCHEDULER_INVALID_CPU, entries[index].cpu);
        }
    }
    TEST_ASSERT_TRUE(found_unassigned);

    ProcessPtr idle = unwrap_optional(Scheduler__idle_process(scheduler, scheduled_cpu), (ProcessPtr){});

    Scheduler__start(scheduler, scheduled_cpu);
    // should start with idle
    TEST_ASSERT_EQUAL(idle, current_running_process());
    TEST_ASSERT_EQUAL(idle->thread.state, PROC_STATE_RUNNING);

    Scheduler__preempt(scheduler, scheduled_cpu, &Some(ThreadContextPtr, context));
    CPUSchedulerPtr assigned_cpu =
        unwrap_optional(CPUSchedulers__get(&scheduler->schedulers, scheduled_cpu), (CPUSchedulerPtr){});
    TEST_ASSERT_EQUAL(1, ThreadTable__len(&assigned_cpu->threads));
    TEST_ASSERT_EQUAL(dummy, current_running_process());
    TEST_ASSERT_EQUAL(0, dummy->thread.last_tick);
    TEST_ASSERT_EQUAL(dummy->thread.state, PROC_STATE_RUNNING);

    match_result(
        Scheduler__cpu_for_process(scheduler, dummy),
        success(cpu)
        {
            TEST_ASSERT_EQUAL_UINT64(scheduled_cpu, cpu);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(context->elr, Process__required_context(idle)->elr); // check the context was saved
    TEST_ASSERT_EQUAL(idle->thread.state, PROC_STATE_INTERRUPTED);         // check the context was saved
    TEST_ASSERT_EQUAL(1, idle->thread.last_tick);                          // check the context was saved

    match_optional(
        Scheduler__current_process(scheduler, scheduled_cpu),
        some(current)
        {
            TEST_ASSERT_EQUAL(dummy, current);
        },
        none_test_fail()
    );
}

void test_scheduler_preempt_when_idle_with_multiple_processes_ready_selects_longest_waiting(void)
{
    ThreadContextPtr context = test_saved_context();

    CPUID dummy_cpu = 11;
    ProcessPtr proc_most_recently_run = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    ProcessPtr proc_2nd_most_recently_run = dummy_process(50, dummy_cpu, PROC_STATE_READY);
    ProcessPtr proc_least_recently_run = dummy_process(0, dummy_cpu, PROC_STATE_READY);

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, proc_most_recently_run, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, proc_2nd_most_recently_run, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, proc_least_recently_run, dummy_cpu);

    Scheduler__preempt(scheduler, dummy_cpu, &Some(ThreadContextPtr, context));
    TEST_ASSERT_EQUAL(proc_least_recently_run, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc_least_recently_run->thread.state);

    match_optional(
        Scheduler__current_process(scheduler, dummy_cpu),
        some(current)
        {
            TEST_ASSERT_EQUAL(proc_least_recently_run, current);
        },
        none_do_nothing()
    );
}

void test_scheduler_preempt_selects_longer_waiting_normal_before_service(void)
{
    ThreadContextPtr context = test_saved_context();

    CPUID dummy_cpu = 11;
    ProcessPtr normal_proc = dummy_process(0, dummy_cpu, PROC_STATE_READY);
    ProcessPtr service_proc = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, normal_proc, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, service_proc, dummy_cpu);

    Scheduler__preempt(scheduler, dummy_cpu, &Some(ThreadContextPtr, context));
    TEST_ASSERT_EQUAL(normal_proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, normal_proc->thread.state);
}

void test_scheduler_preempt_selects_longer_waiting_service_before_normal(void)
{
    ThreadContextPtr context = test_saved_context();
    CPUID dummy_cpu = 11;
    ProcessPtr normal_proc = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    ProcessPtr service_proc = dummy_process(0, dummy_cpu, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, normal_proc, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, service_proc, dummy_cpu);

    Scheduler__preempt(scheduler, dummy_cpu, &Some(ThreadContextPtr, context));
    TEST_ASSERT_EQUAL(service_proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, service_proc->thread.state);
}

void test_scheduler_preempt_selects_realtime_service_before_service_process(void)
{
    ThreadContextPtr context = test_saved_context();

    CPUID dummy_cpu = 11;
    ProcessPtr service_proc = dummy_process(0, dummy_cpu, PROC_STATE_READY);
    ProcessPtr realtime_proc = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;
    realtime_proc->scheduling.process_class = PROCESS_CLASS_REALTIME_SERVICE;

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, service_proc, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, realtime_proc, dummy_cpu);

    Scheduler__preempt(scheduler, dummy_cpu, &Some(ThreadContextPtr, context));
    TEST_ASSERT_EQUAL(realtime_proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, realtime_proc->thread.state);
}

void test_realtime_promotion_keeps_process_on_cpu_and_allows_other_work(void)
{
    CPUID scheduled_cpu = 4;
    ProcessPtr realtime_proc = dummy_process(100, scheduled_cpu, PROC_STATE_READY);
    ProcessPtr normal_proc = dummy_process(101, scheduled_cpu, PROC_STATE_READY);

    Scheduler__start(scheduler, scheduled_cpu);
    match_result(Scheduler__schedule_on_cpu(scheduler, realtime_proc, scheduled_cpu),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, normal_proc, scheduled_cpu),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Scheduler__promote_realtime(scheduler, realtime_proc),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROCESS_CLASS_REALTIME_SERVICE, realtime_proc->scheduling.process_class);
    match_result(
        Scheduler__cpu_for_process(scheduler, realtime_proc),
        success(owner_cpu)
        {
            TEST_ASSERT_EQUAL_UINT64(scheduled_cpu, owner_cpu);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__queue_wait(scheduler, &realtime_proc->thread,
                              IpcEvent__generation(IpcDoorbell__event(k_v_ptr(
                                  unwrap_result(Process__doorbell(realtime_proc), NULL_USER_ADDRESS), IpcDoorbell))),
                              QUEUE_WAIT_INDEFINITE_TICKS),
        success(status)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, status);
        },
        error_test_fail()
    );
    Scheduler__preempt(scheduler, scheduled_cpu, &Some(ThreadContextPtr, test_saved_context()));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, realtime_proc->thread.state);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, normal_proc->thread.state);
}

void test_pending_termination_overrides_active_irq_timer_suppression(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessID pid = service_proc->pid;
    IRQNumber irq = 33;
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;
    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, test_saved_context()));
    match_result(Scheduler__subscribe_irq(scheduler, service_proc, irq),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__send_irq_event(scheduler, irq),
        success_do_nothing(),
        error_test_fail()
    );
    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(test_service_wait(service_proc, &kernel_context),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(Scheduler__should_preempt_on_timer(scheduler, cpu_id));
    match_result(Scheduler__queue_signal(scheduler, pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Scheduler__should_preempt_on_timer(scheduler, cpu_id));
    test_timer_interrupt(irq, cpu_id, test_saved_context(), false);
    match_optional(Scheduler__find_process_by_pid(scheduler, pid),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_pending_signals_preserve_other_bits_and_prioritize_kill(void)
{
    ProcessPtr process = dummy_process(100, cpu_id, PROC_STATE_READY);
    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);
    Process__queue_signal(process, PROCESS_SIGNAL_TERMINATE);
    Process__queue_signal(process, PROCESS_SIGNAL_ABORT);
    Process__queue_signal(process, PROCESS_SIGNAL_KILL);
    match_optional(
        Process__take_pending_signal(process),
        some(signal)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_KILL, signal);
        },
        none_test_fail()
    );
    TEST_ASSERT_TRUE(Process__has_pending_signal(process));
    match_optional(
        Process__take_pending_signal(process),
        some(signal)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_INTERRUPT, signal);
        },
        none_test_fail()
    );
    match_optional(
        Process__take_pending_signal(process),
        some(signal)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_ABORT, signal);
        },
        none_test_fail()
    );
    match_optional(
        Process__take_pending_signal(process),
        some(signal)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_TERMINATE, signal);
        },
        none_test_fail()
    );
    TEST_ASSERT_FALSE(Process__has_pending_signal(process));
    match_optional(Process__take_pending_signal(process),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_stop_and_continue_update_scheduler_state_and_parent_events(void)
{
    ProcessPtr parent = dummy_process(90, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process_with_parent(91, cpu_id, PROC_STATE_RUNNING, parent);
    Scheduler__schedule_on_cpu(scheduler, parent, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, child, cpu_id);
    child->thread.state = PROC_STATE_RUNNING;
    ThreadContext context = *Process__required_context(child);

    Process__queue_signal(child, PROCESS_SIGNAL_TERMINAL_STOP);
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_STOP,
                      Scheduler__deliver_pending_signal(scheduler, &child->thread, &context));
    TEST_ASSERT_EQUAL(PROC_STATE_STOPPED, child->thread.state);
    TEST_ASSERT_FALSE(Process__has_pending_resume_signal(child));
    match_result(
        Scheduler__child_take_event(scheduler, parent, child->pid, PROCESS_CHILD_WAIT_UNTRACED),
        success(event)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_STOPPED, event.type);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_WAIT_STATUS_STOPPED(PROCESS_SIGNAL_TERMINAL_STOP), event.status);
        },
        error_test_fail()
    );

    match_result(Scheduler__queue_signal(scheduler, child->pid, PROCESS_SIGNAL_CONTINUE),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, child->thread.state);
    match_result(
        Scheduler__child_take_event(scheduler, parent, child->pid, PROCESS_CHILD_WAIT_CONTINUED),
        success(event)
        {
            TEST_ASSERT_EQUAL(PROCESS_CHILD_EVENT_CONTINUED, event.type);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_WAIT_STATUS_CONTINUED, event.status);
        },
        error_test_fail()
    );
}

void test_stopped_process_only_becomes_runnable_for_continue_or_kill(void)
{
    ProcessPtr process = dummy_process(92, cpu_id, PROC_STATE_STOPPED);
    Scheduler__schedule_on_cpu(scheduler, process, cpu_id);
    process->thread.state = PROC_STATE_STOPPED;

    match_result(Scheduler__queue_signal(scheduler, process->pid, PROCESS_SIGNAL_USER_1),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_STOPPED, process->thread.state);

    match_result(Scheduler__queue_signal(scheduler, process->pid, PROCESS_SIGNAL_KILL),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, process->thread.state);
}

void test_process_group_changes_enforce_session_child_and_exec_rules(void)
{
    ProcessPtr caller = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr group_member = dummy_process(101, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process_with_parent(102, cpu_id, PROC_STATE_READY, caller);
    caller->session_id = 10;
    caller->process_group_id = 20;
    group_member->session_id = 10;
    group_member->process_group_id = group_member->pid;
    child->session_id = 10;
    child->process_group_id = 20;
    child->lifecycle.execed_since_fork = false;
    match_result(Scheduler__schedule_on_cpu(scheduler, caller, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, group_member, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__schedule_on_cpu(scheduler, child, cpu_id),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        Scheduler__set_process_group(scheduler, caller, child->pid, group_member->pid),
        success(group)
        {
            TEST_ASSERT_EQUAL_UINT64(group_member->pid, group);
        },
        error_test_fail()
    );
    child->lifecycle.execed_since_fork = true;
    match_result(
        Scheduler__set_process_group(scheduler, caller, child->pid, child->pid),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_GROUP_PERMISSION_DENIED, code);
        }
    );
    match_result(
        Scheduler__set_process_group(scheduler, caller, 0, 999),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_GROUP_PERMISSION_DENIED, code);
        }
    );

    match_result(
        Scheduler__create_session(scheduler, caller),
        success(session)
        {
            TEST_ASSERT_EQUAL_UINT64(caller->pid, session);
            TEST_ASSERT_EQUAL_UINT64(caller->pid, caller->session_id);
            TEST_ASSERT_EQUAL_UINT64(caller->pid, caller->process_group_id);
        },
        error_test_fail()
    );
    match_result(
        Scheduler__create_session(scheduler, caller),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_GROUP_PERMISSION_DENIED, code);
        }
    );
}

void test_group_signal_delivery_obeys_target_and_identity_permissions(void)
{
    ProcessPtr sender = dummy_process(110, cpu_id, PROC_STATE_READY);
    ProcessPtr peer = dummy_process(111, cpu_id, PROC_STATE_READY);
    ProcessPtr denied = dummy_process(112, cpu_id, PROC_STATE_READY);
    sender->session_id = 50;
    sender->process_group_id = 60;
    peer->session_id = 50;
    peer->process_group_id = 60;
    denied->session_id = 50;
    denied->process_group_id = 60;
    Process__set_identity(sender, 100, 100, PROCESS_CAP_NONE);
    Process__set_identity(peer, 100, 100, PROCESS_CAP_NONE);
    Process__set_identity(denied, 200, 200, PROCESS_CAP_NONE);
    Scheduler__schedule_on_cpu(scheduler, sender, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, peer, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, denied, cpu_id);

    match_result(
        Scheduler__send_signal(scheduler, sender, 0, PROCESS_SIGNAL_USER_1),
        success(delivered)
        {
            TEST_ASSERT_EQUAL_UINT64(2, delivered);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Process__has_pending_signal(sender));
    TEST_ASSERT_TRUE(Process__has_pending_signal(peer));
    TEST_ASSERT_FALSE(Process__has_pending_signal(denied));

    match_result(
        Scheduler__send_signal(scheduler, sender, denied->pid, PROCESS_SIGNAL_CONTINUE),
        success(delivered)
        {
            TEST_ASSERT_EQUAL_UINT64(1, delivered);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Process__has_pending_resume_signal(denied));
}

void test_terminal_interrupt_requires_capability_and_group_target(void)
{
    ProcessPtr sender = dummy_process(113, cpu_id, PROC_STATE_READY);
    ProcessPtr target = dummy_process(114, cpu_id, PROC_STATE_READY);
    sender->process_group_id = 113;
    target->process_group_id = 114;
    Process__set_identity(sender, PROCESS_USER_HUND, PROCESS_GROUP_HUND, PROCESS_CAP_NONE);
    Process__set_root_identity(target);
    Scheduler__schedule_on_cpu(scheduler, sender, cpu_id);
    Scheduler__schedule_on_cpu(scheduler, target, cpu_id);

    match_result(
        Scheduler__send_signal(scheduler, sender, -(int64_t)target->process_group_id, PROCESS_SIGNAL_INTERRUPT),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_PERMISSION_DENIED, code);
        }
    );
    match_result(
        Scheduler__send_terminal_interrupt(scheduler, sender, -(int64_t)target->process_group_id),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_PERMISSION_DENIED, code);
        }
    );
    sender->capabilities = PROCESS_CAP_SIGNAL_TERMINAL;
    match_result(
        Scheduler__send_terminal_interrupt(scheduler, sender, target->pid),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_INVALID_ARGUMENT, code);
        }
    );
    match_result(
        Scheduler__send_terminal_interrupt(scheduler, sender, -(int64_t)target->process_group_id),
        success(delivered)
        {
            TEST_ASSERT_EQUAL_UINT64(1, delivered);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(Process__has_pending_signal(target));
    TEST_ASSERT_FALSE(Process__has_pending_signal(sender));
}

void test_signal_suspend_atomically_restores_mask_and_wakes_for_handler(void)
{
    ProcessPtr process = dummy_process(120, cpu_id, PROC_STATE_READY);
    Scheduler__schedule_on_cpu(scheduler, process, cpu_id);
    ProcessSignalMask original = 1ULL << (PROCESS_SIGNAL_USER_1 - 1);
    ProcessSignalMask temporary = 1ULL << (PROCESS_SIGNAL_USER_2 - 1);
    ProcessSignalAction action = ProcessSignalAction__new(0x1000, 0x2000, 0, 0);
    match_result(Process__set_signal_mask(process, PROCESS_SIGNAL_MASK_SET, original),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Process__set_signal_action(process, PROCESS_SIGNAL_INTERRUPT, &action),
        success_do_nothing(),
                 error_test_fail()
    );

    match_result(Scheduler__signal_suspend(scheduler, &process->thread, temporary),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->thread.state);
    TEST_ASSERT_EQUAL_UINT64(temporary, Process__signal_mask(process));
    match_result(Scheduler__queue_signal(scheduler, process->pid, PROCESS_SIGNAL_INTERRUPT),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, process->thread.state);
    TEST_ASSERT_EQUAL_UINT64(original, Process__signal_mask(process));
    TEST_ASSERT_TRUE(Process__has_ready_parked_operation(process, PARK_WAIT_SIGNAL, &None(OperationKey)));
}

void test_scheduler_timer_preemption_is_suppressed_for_active_irq_service(void)
{
    ThreadContextPtr context = test_saved_context();
    CPUID scheduled_cpu = 4;
    IRQNumber irq = 33;

    ProcessPtr service_proc = dummy_process(0, scheduled_cpu, PROC_STATE_READY);
    ProcessPtr normal_proc = dummy_process(100, scheduled_cpu, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    Scheduler__schedule_on_cpu(scheduler, service_proc, scheduled_cpu);
    Scheduler__start(scheduler, scheduled_cpu);
    Scheduler__preempt(scheduler, scheduled_cpu, &Some(ThreadContextPtr, context));
    Scheduler__schedule_on_cpu(scheduler, normal_proc, scheduled_cpu);

    TEST_ASSERT_EQUAL(service_proc, current_running_process());

    match_result(
        Scheduler__subscribe_irq(scheduler, service_proc, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(
        test_service_wait(service_proc, &kernel_context),
        success(event)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
        },
        error_test_fail()
    );

    TEST_ASSERT_TRUE(Process__is_handling_service_irq(service_proc));

    TEST_ASSERT_FALSE(Scheduler__should_preempt_on_timer(scheduler, scheduled_cpu));

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    TEST_ASSERT_TRUE(Scheduler__should_preempt_on_timer(scheduler, scheduled_cpu));
}

void test_timer_handler_does_not_preempt_active_irq_service(void)
{
    ThreadContextPtr context = test_saved_context();
    CPUID scheduled_cpu = 4;
    IRQNumber irq = 33;

    ProcessPtr service_proc = dummy_process(0, scheduled_cpu, PROC_STATE_READY);
    ProcessPtr normal_proc = dummy_process(100, scheduled_cpu, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    Scheduler__schedule_on_cpu(scheduler, service_proc, scheduled_cpu);
    Scheduler__start(scheduler, scheduled_cpu);
    Scheduler__preempt(scheduler, scheduled_cpu, &Some(ThreadContextPtr, context));
    Scheduler__schedule_on_cpu(scheduler, normal_proc, scheduled_cpu);

    match_result(
        Scheduler__subscribe_irq(scheduler, service_proc, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );
    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(
        test_service_wait(service_proc, &kernel_context),
        success(event)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
        },
        error_test_fail()
    );

    test_timer_interrupt(irq, scheduled_cpu, context, false);

    TEST_ASSERT_EQUAL(service_proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, service_proc->thread.state);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, normal_proc->thread.state);
}

void test_process_class_names_are_available_for_scheduler_snapshots(void)
{
    TEST_ASSERT_EQUAL_STRING("normal", ProcessClass__to_string(PROCESS_CLASS_NORMAL));
    TEST_ASSERT_EQUAL_STRING("service", ProcessClass__to_string(PROCESS_CLASS_SERVICE));
    TEST_ASSERT_EQUAL_STRING("realtime_service", ProcessClass__to_string(PROCESS_CLASS_REALTIME_SERVICE));
}

void test_service_wait_parks_service_process_when_no_event_is_available(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, service_proc->thread.state);
    match_optional(
        Process__top_park_reason(service_proc),
        some(reason)
        {
            TEST_ASSERT_EQUAL(PARK_WAIT_SERVICE_EVENT, reason);
        },
        none_test_fail()
    );
}

void test_service_enqueue_wakes_parked_service_process_and_wait_returns_event(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    ServiceEvent irq_event = (ServiceEvent){
        .type = SERVICE_EVENT_IRQ,
        .source = 32,
        .data = 99,
    };

    match_result(
        Scheduler__service_enqueue(scheduler, service_proc, &irq_event),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->thread.state);

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success(event)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(32, event.source);
            TEST_ASSERT_EQUAL(99, event.data);
        },
        error_test_fail()
    );
}

void test_service_enqueue_makes_waiting_service_runnable_when_event_is_queued(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    service_proc->thread.state = PROC_STATE_WAITING;

    ServiceEvent irq_event = (ServiceEvent){
        .type = SERVICE_EVENT_IRQ,
        .source = 32,
        .data = 99,
    };

    match_result(
        Scheduler__service_enqueue(scheduler, service_proc, &irq_event),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->thread.state);
    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service_state.service.events));
}

void test_scheduler_irq_event_wakes_parked_service_process(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 39;

    KernelContext kernel_context = test_scheduler_kernel_context();

    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);

    match_result(
        Scheduler__subscribe_irq(scheduler, service_proc, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, service_proc->thread.state);

    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->thread.state);

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success(event)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_EQUAL(0, event.data);
        },
        error_test_fail()
    );
}

void test_scheduler_subscribed_irq_is_sent_to_service_process(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;

    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);

    match_result(
        Scheduler__subscribe_irq(scheduler, service_proc, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(
        test_service_wait(service_proc, &kernel_context),
        success(event)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_EQUAL(0, event.data);
        },
        error_test_fail()
    );
}

void test_scheduler_subscribed_irq_coalesces_while_pending_until_process_completes(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->scheduling.process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;

    Scheduler__schedule_on_cpu(scheduler, service_proc, cpu_id);

    match_result(
        Scheduler__subscribe_irq(scheduler, service_proc, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service_state.service.events));
    TEST_ASSERT_EQUAL(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(
        test_service_wait(service_proc, &kernel_context),
        success(event)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_TRUE(Process__is_handling_service_irq(service_proc));
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    match_result(
        test_service_wait(service_proc, &kernel_context),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    TEST_ASSERT_FALSE(Process__is_handling_service_irq(service_proc));
    TEST_ASSERT_EQUAL(irq, Arch__unmasked_irq());
    TEST_ASSERT_EQUAL(1, Arch__unmask_count());

    match_result(
        Scheduler__send_irq_event(scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL_INT(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service_state.service.events));
    TEST_ASSERT_EQUAL(2, Arch__mask_count());
}

void test_scheduler_interrupt_switch_from_one_ready_process_to_another(void)
{
    ThreadContextPtr context = test_saved_context();

    CPUID dummy_cpu = 11;
    ProcessPtr proc_most_recently_run = dummy_process(1000, dummy_cpu, PROC_STATE_READY);
    ProcessPtr proc_least_recently_run = dummy_process(0, dummy_cpu, PROC_STATE_READY);

    Scheduler__start(scheduler, dummy_cpu);

    Scheduler__schedule_on_cpu(scheduler, proc_most_recently_run, dummy_cpu);
    Scheduler__schedule_on_cpu(scheduler, proc_least_recently_run, dummy_cpu);

    Scheduler__preempt(scheduler, dummy_cpu, &Some(ThreadContextPtr, context));
    TEST_ASSERT_EQUAL(proc_least_recently_run, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_READY, proc_most_recently_run->thread.state);
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc_least_recently_run->thread.state);

    Scheduler__preempt(scheduler, dummy_cpu, &Some(ThreadContextPtr, context));
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc_most_recently_run->thread.state);
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, proc_least_recently_run->thread.state);
    TEST_ASSERT_EQUAL(proc_most_recently_run, current_running_process());
}

void test_scheduler_park_by_pid_puts_process_in_wait(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    Scheduler__start(scheduler, cpu_id);

    Scheduler__schedule(scheduler, proc);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    ProcessPtr running = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_EQUAL(proc, running);

    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(
        Scheduler__park_by_pid(scheduler, proc->pid, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    running = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_NOT_EQUAL(proc, running);

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, proc->thread.state);
}

void test_scheduler_unpark_by_pid_resumes_io_continuation(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ThreadContextPtr context = test_saved_context();

    Scheduler__start(scheduler, cpu_id);

    Scheduler__schedule(scheduler, proc);
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    ProcessPtr running = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_EQUAL(proc, running);

    KernelContext kernel_context = test_scheduler_kernel_context();
    match_result(
        Scheduler__park_by_pid(scheduler, proc->pid, &kernel_context, &Some(Continuation, continuation), PARK_WAIT_IO),
        success_do_nothing(),
        error_test_fail()
    );
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    running = unwrap_optional(Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_NOT_EQUAL(proc, running);

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, proc->thread.state);

    match_result(Scheduler__unpark_by_pid(scheduler, proc->pid, PARK_WAIT_IO, &None(ReturnCode)),
        success_do_nothing(),
                 error_test_fail()
    );
    Scheduler__preempt(scheduler, cpu_id, &Some(ThreadContextPtr, context));

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->thread.state);
    TEST_ASSERT_EQUAL(&kernel_context, continuation_kernel_context);
}

#include "unity.h"
#include "../stubs/panic.h"
#include "../stubs/cpu.h"
#include "../stubs/process.h"
#include "../stubs/process_context_layout.h"
#include "../fixtures/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/scheduler/register.h"
#include "../../../kernel/service/service.h"
#include "../../../kernel/subsystems/message/message_broker.h"
#include "../../../kernel/subsystems/message/subsystem_codes.h"
#include "../../../kernel/subsystems/io/io_subsystem.h"
#include "../fixtures/kernel.h"

#include <string.h>

static SchedulerPtr scheduler = NULL;
static KernelPtr kernel = NULL;
static ParkedOperationPtr continuation_context;
static KernelContextPtr continuation_kernel_context;
static Optional(Message) received_message = None(Message);
static bool abort_stderr_written = false;
static char abort_stderr_buffer[512];
static ByteCount abort_stderr_size = 0;
static ProcessPtr expected_active_process = NULL;
static bool continuation_saw_active_address_space = false;

static const CPUID cpu_id = 12;

void Arch__reset_irq_tracking();
uint64_t Arch__masked_irq();
uint64_t Arch__unmasked_irq();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();

static BString no_return_code = BSTRING_NEW("no return code");
static BString abort_stderr_path = BSTRING_NEW(":abort-stderr:");

static Result(IOInternalHandle) abort_stderr_open(IOStatePtr state, BString path, Flags flags)
{
    return Success(IOInternalHandle, 0x1234);
}

static Result(IOInternalHandle) abort_stderr_close(IOStatePtr state, IOInternalHandle handle)
{
    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) abort_stderr_read(
    IOStatePtr state, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    return Error(IOBufferPtr, bstring("unsupported"), IO_OPERATION_NOT_SUPPORTED);
}

static Result(ByteCount) abort_stderr_write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    abort_stderr_written = true;
    abort_stderr_size = size;

    size_t copy_size = size;
    if (copy_size >= sizeof(abort_stderr_buffer))
    {
        copy_size = sizeof(abort_stderr_buffer) - 1;
    }

    for (size_t i = 0; i < copy_size; i++)
    {
        abort_stderr_buffer[i] = data[i];
    }
    abort_stderr_buffer[copy_size] = '\0';

    return Success(ByteCount, size);
}

static Result(ParkStatus) abort_stderr_park(IOStatePtr state, IOContextPtr parked_context)
{
    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) abort_stderr_unpark(IOStatePtr state)
{
    return Success(ParkStatus, IO_UNPARK_OK);
}

static IO abort_stderr_io = (IO) {
    .open = abort_stderr_open,
    .close = abort_stderr_close,
    .read = abort_stderr_read,
    .write = abort_stderr_write,
    .park = abort_stderr_park,
    .unpark = abort_stderr_unpark,
};

static IOState abort_stderr_state = NEW_IO_STATE;

void setUp(void)
{
    continuation_context = NULL;
    continuation_kernel_context = NULL;
    received_message = None(Message);
    abort_stderr_written = false;
    abort_stderr_size = 0;
    abort_stderr_buffer[0] = '\0';
    expected_active_process = NULL;
    continuation_saw_active_address_space = false;
    clear_panic_state();
    set_cpu_id(cpu_id);
    reset_cpu_count();
    current_process_reset();
    clear_deferred_syscall_result();
    Arch__reset_irq_tracking();
    kernel = init_test_kernel_without_filesystem();
    scheduler = kernel->scheduler;

    Scheduler__register(kernel);
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

    match_optional(ReturnCode, parked_operation->return_code, some(code) { return Success(Anything, code); }, none_do_nothing());

    return Error(Anything, no_return_code, -1);
}

void test_kernel_context_reports_no_state_when_empty()
{
    KernelContext context = KernelContext__new(None(ProcessID), None(SchedulerPtr));

    TEST_ASSERT_FALSE(KernelContext__has_any_state(&context));
}

void test_kernel_context_reports_state_when_field_is_present()
{
    KernelContext context = KernelContext__new(Some(ProcessID, 7), None(SchedulerPtr));

    TEST_ASSERT_TRUE(KernelContext__has_any_state(&context));
}

void test_parked_operation_is_ready_without_blocked_operations()
{
    ParkedOperationPtr operation = ParkedOperation__heap_new_with_context_ownership(
        KernelContext__heap_new(None(ProcessID), None(SchedulerPtr)),
        None(Continuation),
        PARK_WAIT_IO,
        true
    );
    ParkedOperation__mark_ready(operation);

    TEST_ASSERT_TRUE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(0, operation->pending_operations);

    ParkedOperation__dispose(operation);
}

void test_parked_operation_waits_until_all_blocked_operations_resolve_out_of_order()
{
    ParkedOperationPtr operation = ParkedOperation__heap_new_with_context_ownership(
        KernelContext__heap_new(None(ProcessID), None(SchedulerPtr)),
        None(Continuation),
        PARK_WAIT_IO,
        true
    );
    ParkedOperation__mark_ready(operation);
    ParkedOperationPtr first_dependency = ParkedOperation__heap_new_with_context_ownership(
        KernelContext__heap_new(None(ProcessID), None(SchedulerPtr)),
        None(Continuation),
        PARK_WAIT_IO,
        true
    );
    ParkedOperationPtr second_dependency = ParkedOperation__heap_new_with_context_ownership(
        KernelContext__heap_new(None(ProcessID), None(SchedulerPtr)),
        None(Continuation),
        PARK_WAIT_IO,
        true
    );

    match_result(CollectionCode, Scheduler__add_blocked_operation(scheduler, operation, first_dependency), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, Scheduler__add_blocked_operation(scheduler, operation, second_dependency), success_do_nothing(), error_test_fail());

    TEST_ASSERT_FALSE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(2, operation->pending_operations);

    match_result(CollectionCode, Scheduler__resolve_blocked_operation(scheduler, operation, second_dependency), success_do_nothing(), error_test_fail());

    TEST_ASSERT_FALSE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(1, operation->pending_operations);
    TEST_ASSERT_EQUAL_UINT64(1, BlockedParkedOperations__len(&operation->blocked_operations));

    match_result(CollectionCode, Scheduler__resolve_blocked_operation(scheduler, operation, first_dependency), success_do_nothing(), error_test_fail());

    TEST_ASSERT_TRUE(Scheduler__parked_operation_is_ready(scheduler, operation));
    TEST_ASSERT_EQUAL_UINT64(0, operation->pending_operations);
    TEST_ASSERT_EQUAL_UINT64(0, BlockedParkedOperations__len(&operation->blocked_operations));

    ParkedOperation__dispose(operation);
}

void test_parked_operation_disposes_unresolved_blocked_operations()
{
    ParkedOperationPtr operation = ParkedOperation__heap_new_with_context_ownership(
        KernelContext__heap_new(None(ProcessID), None(SchedulerPtr)),
        None(Continuation),
        PARK_WAIT_IO,
        true
    );
    ParkedOperation__mark_ready(operation);
    ParkedOperationPtr blocked_operation = ParkedOperation__heap_new_with_context_ownership(
        KernelContext__heap_new(None(ProcessID), None(SchedulerPtr)),
        None(Continuation),
        PARK_WAIT_IO,
        true
    );

    match_result(CollectionCode, ParkedOperation__add_blocked_operation(operation, blocked_operation), success_do_nothing(), error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(1, operation->pending_operations);
    TEST_ASSERT_EQUAL_UINT64(1, BlockedParkedOperations__len(&operation->blocked_operations));

    ParkedOperation__dispose(operation);
}

static void install_abort_stderr(ProcessPtr proc)
{
    match_result(DeviceHandle, IOSubsystem__register_device(
        kernel->io_subsystem, abort_stderr_path, &abort_stderr_io, &abort_stderr_state
    ), success_do_nothing(), error_test_fail());

    KernelContext kernel_context = KernelContext__new(
        Some(ProcessID, proc->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr context = IOContext__heap_new(
        kernel_context,
        Some(IOSubsystemPtr, kernel->io_subsystem),
        None(IOHandle),
        None(ExternalBuffer)
    );

    match_result(IOHandle, IOSubsystem__open(kernel->io_subsystem, context, abort_stderr_path, IO_OPEN_WRITE), success(handle) {
            match_result(FileDescriptor, Process__set_file_descriptor(proc, 2, handle), success_do_nothing(), error_test_fail());
        }, error_test_fail());

    IOContext__dispose(context);
}

void test_scheduler_interrupt_when_no_processes_are_running_stays_in_idle(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref
    Scheduler__start(scheduler, cpu_id);

    Scheduler__preempt(scheduler, cpu_id, context);
    ProcessPtr idle = Unwrap(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});
    ProcessPtr current = Unwrap(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});

    TEST_ASSERT_EQUAL(idle, current);
    TEST_ASSERT_EQUAL(idle->context, context);
    TEST_ASSERT_EQUAL(idle->state, PROC_STATE_RUNNING);
    TEST_ASSERT_EQUAL(current_running_process(), idle);
}

void test_scheduler_interrupt_before_cpu_start_does_not_context_switch(void)
{
    CPUID unstarted_cpu = 1;
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref
    ProcessPtr idle = Unwrap(ProcessPtr, Scheduler__idle_process(scheduler, unstarted_cpu), (ProcessPtr){});

    current_process_reset();

    Scheduler__preempt(scheduler, unstarted_cpu, context);

    TEST_ASSERT_NULL(current_running_process());
    TEST_ASSERT_NOT_EQUAL(PROC_STATE_RUNNING, idle->state);
}

void test_scheduler_init_creates_cpu_slots_independent_of_online_cpu_count(void)
{
    set_cpu_count(MAX_CPUS);

    MessageBrokerPtr message_broker = MessageBroker__heap_new();
    SchedulerPtr local_scheduler = Scheduler__heap_new(message_broker);
    Elf idle = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    SystemInfo system_info = (SystemInfo) {
        .cpu_count = 1,
    };
    kernel->system_info = Some(SystemInfo, system_info);

    match_result(SchedulerInitStatus, Scheduler__init(kernel, local_scheduler, idle), success_do_nothing(), error_test_fail());

    match_optional(ProcessPtr, Scheduler__idle_process(local_scheduler, 0), some(idle_process) { TEST_ASSERT_NOT_NULL(idle_process); }, none_test_fail());

    match_optional(ProcessPtr, Scheduler__idle_process(local_scheduler, 1), some_test_fail(), none_do_nothing());

    reset_cpu_count();
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
    match_optional(ArrayIndex, ProcessTable__find_item(processes, proc1), some_do_nothing(), none_test_fail());
    match_optional(ArrayIndex, ProcessTable__find_item(processes, proc2), some_do_nothing(), none_test_fail());
    match_optional(ArrayIndex, ProcessTable__find_item(processes, proc3), some_do_nothing(), none_test_fail());

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

    match_optional(ProcessPtr, Scheduler__save_context(scheduler, cpu_id, context), some(proc) {
            TEST_ASSERT_EQUAL(context, proc->context);

            match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) { TEST_ASSERT_EQUAL(context, current->context); }, none_test_fail());
        }, none_test_fail());
}

void test_scheduler_exit_removes_process_when_its_current(void)
{
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr idle = Unwrap(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});

    match_result(ProcessPtr, Scheduler__exit(scheduler, proc, 10), success(exited) {
            TEST_ASSERT_EQUAL(proc, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        }, error_test_fail());

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(idle, current);
        }, none_test_fail());

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(0, ProcessTable__len(processes));
}

void fake_callback(KernelListenerPtr listener, MessagePtr message)
{
    received_message = Some(Message, *message);
}

void test_scheduler_exit_sends_a_message_to_the_broker_saying_the_process_exited(void)
{
    Listener listener = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback);
    MessageBroker__register_listener(kernel->message_broker, listener);

    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    match_result(ProcessPtr, Scheduler__exit(scheduler, proc, 10), success_do_nothing(), error_test_fail());

    TEST_ASSERT_TRUE(received_message.present);
    TEST_ASSERT_EQUAL(SOURCE_PROCESS, received_message.some.val.source);
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, received_message.some.val.source_code);

    Payload payload = Unwrap(Payload, received_message.some.val.payload, (Payload){});
    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_PROCESS_ID, payload.type);
    TEST_ASSERT_EQUAL(proc->pid, payload.process_id);

}

void test_scheduler_exit_unparks_parent_if_waiting(void)
{
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process(1, cpu_id, PROC_STATE_READY);
    child->parent = Some(ProcessPtr, parent);

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));


    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__schedule(scheduler, parent);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);
    Scheduler__schedule(scheduler, child);
    match_result(ProcessPtr, Scheduler__park(scheduler, parent, &kernel_context, Some(Continuation, continuation), PARK_WAIT_CHILD), success_do_nothing(), error_test_fail());
    Scheduler__preempt(scheduler, cpu_id, context);

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(child, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->state);
        }, none_test_fail());

    match_result(ProcessPtr, Scheduler__exit(scheduler, child, 10), success(exited) {
            TEST_ASSERT_EQUAL(child, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        }, error_test_fail());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);

    Anything return_code = result_unwrap(Anything, deferred_syscall_result(), (Anything){});
    TEST_ASSERT_EQUAL(10, return_code); // return code should have been set for switching context

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(parent, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->state);
        }, none_test_fail());
}

void test_scheduler_abort_process_records_fault_writes_stderr_and_unparks_parent(void)
{
    ProcessPtr parent = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessPtr child = dummy_process(2, cpu_id, PROC_STATE_READY);
    child->parent = Some(ProcessPtr, parent);
    install_abort_stderr(child);

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF;
    Scheduler__schedule(scheduler, parent);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);
    Scheduler__schedule(scheduler, child);
    match_result(ProcessPtr, Scheduler__park(scheduler, parent, &kernel_context, Some(Continuation, continuation), PARK_WAIT_CHILD), success_do_nothing(), error_test_fail());
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessAbort abort = (ProcessAbort) {
        .reason = PROCESS_ABORT_DATA_ABORT,
        .pc = 0x400100,
        .fault_address = 0x0,
        .exception_code = 0x24,
        .syndrome = 0x96000004,
        .cpu = cpu_id,
    };

    match_result(ProcessPtr, Scheduler__abort_process(scheduler, kernel->io_subsystem, child, abort), success(aborted) {
            TEST_ASSERT_EQUAL(child, aborted);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, aborted->state);
        }, error_test_fail());

    match_optional(ProcessAbort, child->abort, some(recorded) {
            TEST_ASSERT_EQUAL(PROCESS_ABORT_DATA_ABORT, recorded.reason);
            TEST_ASSERT_EQUAL_HEX64(0x400100, recorded.pc);
            TEST_ASSERT_EQUAL_HEX64(0x96000004, recorded.syndrome);
        }, none_test_fail());

    TEST_ASSERT_TRUE(abort_stderr_written);
    TEST_ASSERT_TRUE(abort_stderr_size > 0);
    TEST_ASSERT_NOT_NULL(strstr(abort_stderr_buffer, "process fault: data abort"));
    TEST_ASSERT_NOT_NULL(strstr(abort_stderr_buffer, "pc=0x400100"));

    Anything return_code = result_unwrap(Anything, deferred_syscall_result(), (Anything){});
    TEST_ASSERT_EQUAL(PROCESS_ABORT_EXIT_BASE - PROCESS_ABORT_DATA_ABORT, return_code);
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

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(current, other_proc);
        }, none_test_fail());


    match_result(ProcessPtr, Scheduler__exit(scheduler, proc, 10), success(exited) {
            TEST_ASSERT_EQUAL(proc, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        }, error_test_fail());

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(other_proc, current);
        }, none_test_fail());

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(1, ProcessTable__len(processes));
}

void test_scheduler_park_switches_process_to_waiting_and_switches_context(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr idle = Unwrap(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(current, proc);
        }, none_test_fail());


    match_result(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        }, error_test_fail());

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(idle, current);
        }, none_test_fail());

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(1, ProcessTable__len(processes));
}

void test_scheduler_unpark_switches_process_back_to_ready(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(current, proc);
        }, none_test_fail());

    match_result(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        }, error_test_fail());

    match_result(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, Some(ReturnCode, 11)), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->state);
        }, error_test_fail());
}

void test_scheduler_sleep_usec_wakes_after_rounded_timer_ticks(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContext context = {0};

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &context);

    match_result(ProcessPtr, Scheduler__sleep_usec(scheduler, proc, 2500), success(sleeping) {
            TEST_ASSERT_EQUAL(proc, sleeping);
            TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, sleeping->state);
        }, error_test_fail());

    Scheduler__preempt(scheduler, cpu_id, &context);
    TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, proc->state);

    Scheduler__preempt(scheduler, cpu_id, &context);
    TEST_ASSERT_EQUAL(PROC_STATE_SLEEPING, proc->state);

    Scheduler__preempt(scheduler, cpu_id, &context);
    TEST_ASSERT_EQUAL(proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->state);
    TEST_ASSERT_EQUAL(PROCESS_SLEEP_OK, result_unwrap(Anything, deferred_syscall_result(), (Anything){}));
}

void test_scheduler_context_switch_finalises_ready_parked_operation_before_resume(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContext context = {0};

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, &context);

    match_result(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO), success_do_nothing(), error_test_fail());

    match_result(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, Some(ReturnCode, 11)), success_do_nothing(), error_test_fail());

    expected_active_process = proc;
    Scheduler__preempt(scheduler, cpu_id, &context);

    TEST_ASSERT_EQUAL(proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->state);
    TEST_ASSERT_EQUAL(&kernel_context, continuation_kernel_context);
    TEST_ASSERT_TRUE(continuation_saw_active_address_space);
    TEST_ASSERT_EQUAL(11, proc->context->x[0]);
    TEST_ASSERT_EQUAL(11, result_unwrap(Anything, deferred_syscall_result(), (Anything){}));
}

void test_scheduler_unpark_gives_error_and_doesnt_unpark_if_reasons_dont_match(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(current, proc);
        }, none_test_fail());

    match_result(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        }, error_test_fail());

    match_result(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_CHILD, Some(ReturnCode, 11)), success_test_fail(), error_test_ok());
}

void test_scheduler_unpark_when_not_idle(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr running_proc = dummy_process(0, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, proc);
    Scheduler__schedule(scheduler, running_proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(current, running_proc);
        }, none_test_fail());

    match_result(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        }, error_test_fail());

    match_result(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, Some(ReturnCode, 11)), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->state);
        }, error_test_fail());

    // it should cause a context switch as CPU is currently idle
    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(running_proc, current);
        }, none_test_fail());
}

void test_scheduler_park_switches_process_to_waiting_not_switching_context_when_not_current(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessPtr running = dummy_process(0, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = (KernelContext) {
        .scheduler = scheduler,
    };

    Scheduler__schedule(scheduler, proc);
    Scheduler__schedule(scheduler, running);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(current, running);
        }, none_test_fail());


    match_result(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO), success(parked) {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        }, error_test_fail());

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), some(current) {
            TEST_ASSERT_EQUAL(running, current);
        }, none_test_fail());

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(2, ProcessTable__len(processes));
}

void test_scheduler_preempt_when_idle_with_a_process_ready(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref
    CPUID scheduled_cpu = 3;

    ProcessPtr dummy = dummy_process(0, scheduled_cpu, PROC_STATE_READY);

    match_result(CPUID, Scheduler__schedule(scheduler, dummy), success(cpu) { TEST_ASSERT_EQUAL(scheduled_cpu, cpu); }, error_test_fail());
    ProcessPtr idle = Unwrap(ProcessPtr, Scheduler__idle_process(scheduler, scheduled_cpu), (ProcessPtr){});

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

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, scheduled_cpu), some(current) {
            TEST_ASSERT_EQUAL(dummy, current);
        }, none_test_fail());
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

    match_optional(ProcessPtr, Scheduler__current_process(scheduler, dummy_cpu), some(current) {
            TEST_ASSERT_EQUAL(proc_least_recently_run, current);
        }, none_do_nothing());
}

void test_scheduler_preempt_selects_service_process_before_normal_process(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref

    CPUID dummy_cpu = 11;
    ProcessPtr normal_proc = dummy_process(0, dummy_cpu, PROC_STATE_READY);
    ProcessPtr service_proc = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule(scheduler, normal_proc);
    Scheduler__schedule(scheduler, service_proc);

    Scheduler__preempt(scheduler, dummy_cpu, context);
    TEST_ASSERT_EQUAL(service_proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, service_proc->state);
}

void test_scheduler_preempt_selects_realtime_service_before_service_process(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000; // whatever as long as we dont deref

    CPUID dummy_cpu = 11;
    ProcessPtr service_proc = dummy_process(0, dummy_cpu, PROC_STATE_READY);
    ProcessPtr realtime_proc = dummy_process(100, dummy_cpu, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    realtime_proc->process_class = PROCESS_CLASS_REALTIME_SERVICE;

    Scheduler__start(scheduler, dummy_cpu);
    Scheduler__schedule(scheduler, service_proc);
    Scheduler__schedule(scheduler, realtime_proc);

    Scheduler__preempt(scheduler, dummy_cpu, context);
    TEST_ASSERT_EQUAL(realtime_proc, current_running_process());
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, realtime_proc->state);
}

void test_scheduler_timer_preemption_is_suppressed_for_active_irq_service(void)
{
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE0000;
    CPUID scheduled_cpu = 4;
    IRQNumber irq = 33;

    ProcessPtr service_proc = dummy_process(0, scheduled_cpu, PROC_STATE_READY);
    ProcessPtr normal_proc = dummy_process(100, scheduled_cpu, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    Scheduler__start(scheduler, scheduled_cpu);
    Scheduler__schedule(scheduler, service_proc);
    Scheduler__schedule(scheduler, normal_proc);
    Scheduler__preempt(scheduler, scheduled_cpu, context);

    TEST_ASSERT_EQUAL(service_proc, current_running_process());

    match_result(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    match_result(ServiceCode, Scheduler__send_irq_event(scheduler, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));
    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success(event) {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
        }, error_test_fail());

    TEST_ASSERT_TRUE(Process__is_handling_service_irq(service_proc));

    TEST_ASSERT_FALSE(Scheduler__should_preempt_on_timer(scheduler, scheduled_cpu));

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code); });

    TEST_ASSERT_TRUE(Scheduler__should_preempt_on_timer(scheduler, scheduled_cpu));
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
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code); });

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, service_proc->state);
    match_optional(ParkReason, Process__top_park_reason(service_proc), some(reason) { TEST_ASSERT_EQUAL(PARK_WAIT_SERVICE_EVENT, reason); }, none_test_fail());
}

void test_service_enqueue_wakes_parked_service_process_and_wait_returns_event(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code); });

    ServiceEvent irq_event = (ServiceEvent) {
        .type = SERVICE_EVENT_IRQ,
        .source = 32,
        .data = 99,
    };

    match_result(ServiceCode, Scheduler__service_enqueue(scheduler, service_proc, irq_event), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->state);

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success(event) {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(32, event.source);
            TEST_ASSERT_EQUAL(99, event.data);
        }, error_test_fail());
}

void test_service_enqueue_makes_waiting_service_runnable_when_event_is_queued(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code); });

    service_proc->state = PROC_STATE_WAITING;

    ServiceEvent irq_event = (ServiceEvent) {
        .type = SERVICE_EVENT_IRQ,
        .source = 32,
        .data = 99,
    };

    match_result(ServiceCode, Scheduler__service_enqueue(scheduler, service_proc, irq_event), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->state);
    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service.events));
}

void test_scheduler_irq_event_wakes_parked_service_process(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 39;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    match_result(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code); });

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, service_proc->state);

    match_result(ServiceCode, Scheduler__send_irq_event(scheduler, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->state);

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success(event) {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_EQUAL(0, event.data);
        }, error_test_fail());
}

void test_scheduler_subscribed_irq_is_sent_to_service_process(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;

    Scheduler__schedule(scheduler, service_proc);

    match_result(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    match_result(ServiceCode, Scheduler__send_irq_event(scheduler, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));
    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success(event) {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_EQUAL(0, event.data);
        }, error_test_fail());
}

void test_scheduler_subscribed_irq_coalesces_while_pending_until_process_completes(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;

    Scheduler__schedule(scheduler, service_proc);

    match_result(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    match_result(ServiceCode, Scheduler__send_irq_event(scheduler, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    match_result(ServiceCode, Scheduler__send_irq_event(scheduler, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service.events));
    TEST_ASSERT_EQUAL(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));
    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success(event) {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_TRUE(Process__is_handling_service_irq(service_proc));
        }, error_test_fail());

    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    match_result(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code); });

    TEST_ASSERT_FALSE(Process__is_handling_service_irq(service_proc));
    TEST_ASSERT_EQUAL(irq, Arch__unmasked_irq());
    TEST_ASSERT_EQUAL(1, Arch__unmask_count());

    match_result(ServiceCode, Scheduler__send_irq_event(scheduler, irq), success(code) { TEST_ASSERT_EQUAL_INT(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service.events));
    TEST_ASSERT_EQUAL(2, Arch__mask_count());
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

void test_io_park_messages_put_process_in_to_wait()
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    Scheduler__start(scheduler, cpu_id);

    Scheduler__schedule(scheduler, proc);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr running = Unwrap(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_EQUAL(proc, running);

    KernelContext kernel_context = (KernelContext) {
        .scheduler = scheduler,
    };


    MessagePtr message =
        Message__heap_new_deferred(
            SOURCE_IO, SUBSYS_IO_PARK, proc->pid, &kernel_context, continuation
        );
    MessageBroker__deliver(kernel->message_broker, message);
    Scheduler__preempt(scheduler, cpu_id, context);

    running = Unwrap(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_NOT_EQUAL(proc, running);

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, proc->state);
}

void test_io_data_arrived_pull_process_out_of_park()
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    Scheduler__start(scheduler, cpu_id);

    Scheduler__schedule(scheduler, proc);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr running = Unwrap(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_EQUAL(proc, running);

    KernelContext kernel_context = (KernelContext) {
        .scheduler = scheduler,
    };

    MessagePtr message_park =
        Message__heap_new_deferred(
            SOURCE_IO, SUBSYS_IO_PARK, proc->pid, &kernel_context, continuation
        );
    MessageBroker__deliver(kernel->message_broker, message_park);
    Scheduler__preempt(scheduler, cpu_id, context);

    running = Unwrap(ProcessPtr, Scheduler__current_process(scheduler, cpu_id), (ProcessPtr){});
    TEST_ASSERT_NOT_EQUAL(proc, running);

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, proc->state);

    MessagePtr message_available =
        Message__heap_new_process_id(SOURCE_IO, SUBSYS_IO_AVAILABLE, proc->pid);
    MessageBroker__deliver(kernel->message_broker, message_available);
    Scheduler__preempt(scheduler, cpu_id, context);

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->state);
    TEST_ASSERT_EQUAL(&kernel_context, continuation_kernel_context);

}

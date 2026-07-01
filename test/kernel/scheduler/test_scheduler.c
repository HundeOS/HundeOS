#include "unity.h"
#include "../stubs/panic.h"
#include "../stubs/cpu.h"
#include "../stubs/process.h"
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
static ParkedKernelContextPtr continuation_context;
static MessagePtr received_message = NULL;
static bool abort_stderr_written = false;
static char abort_stderr_buffer[512];
static ByteCount abort_stderr_size = 0;

static const CPUID cpu_id = 12;

void Arch__reset_irq_tracking();
uint64_t Arch__masked_irq();
uint64_t Arch__unmasked_irq();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();

static KString no_return_code = KSTRING_NEW("no return code");
static KString abort_stderr_path = KSTRING_NEW(":abort-stderr:");

static Result(IOInternalHandle) abort_stderr_open(IOStatePtr state, KString path, Flags flags)
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
    return Error(IOBufferPtr, kstring("unsupported"), IO_OPERATION_NOT_SUPPORTED);
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
    received_message = NULL;
    abort_stderr_written = false;
    abort_stderr_size = 0;
    abort_stderr_buffer[0] = '\0';
    clear_panic_state();
    set_cpu_id(cpu_id);
    reset_cpu_count();
    current_process_reset();
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

Result(Anything) continuation(ParkedKernelContextPtr parked_context)
{
    continuation_context = parked_context;

    Match(ReturnCode, parked_context->return_code)
        IsSome(code, return Success(Anything, code);)
    End

    return Error(Anything, no_return_code, -1);
}

static void install_abort_stderr(ProcessPtr proc)
{
    ResultMatch(DeviceHandle, IOSubsystem__register_device(
        kernel->io_subsystem, abort_stderr_path, &abort_stderr_io, &abort_stderr_state
    ))
        IsError(code, message, TEST_FAIL())
    End

    KernelContext kernel_context = KernelContext__new(
        Some(ProcessID, proc->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr context = IOContext__heap_new(
        kernel_context,
        Some(IOSubsystemPtr, kernel->io_subsystem),
        None(IOHandle),
        None(ExternalBuffer)
    );

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, context, abort_stderr_path, IO_OPEN_WRITE))
        IsSuccess(handle,
        {
            ResultMatch(FileDescriptor, Process__set_file_descriptor(proc, 2, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

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
    set_cpu_count(1);

    MessageBrokerPtr message_broker = MessageBroker__heap_new();
    SchedulerPtr local_scheduler = Scheduler__heap_new(message_broker);
    Elf idle = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});

    ResultMatch(SchedulerInitStatus, Scheduler__init(local_scheduler, idle))
        IsError(code, message, TEST_FAIL())
    End

    Match(ProcessPtr, Scheduler__idle_process(local_scheduler, 1))
        IsSome(idle_process, TEST_ASSERT_NOT_NULL(idle_process);)
        IsNone(TEST_FAIL())
    End

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
    Match(ArrayIndex, ProcessTable__find_item(processes, proc1))
        IsNone(TEST_FAIL())
    End
    Match(ArrayIndex, ProcessTable__find_item(processes, proc2))
        IsNone(TEST_FAIL())
    End
    Match(ArrayIndex, ProcessTable__find_item(processes, proc3))
        IsNone(TEST_FAIL())
    End

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

void test_scheduler_exit_removes_process_when_its_current(void)
{
    ProcessPtr proc = dummy_process(1, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref
    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessPtr idle = Unwrap(ProcessPtr, Scheduler__idle_process(scheduler, cpu_id), (ProcessPtr){});

    ResultMatch(ProcessPtr, Scheduler__exit(scheduler, proc, 10))
        IsError(code, message, TEST_FAIL());
        IsSuccess(exited,
        {
            TEST_ASSERT_EQUAL(proc, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(idle, current);
        })
    End

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(0, ProcessTable__len(processes));
}

void fake_callback(KernelListenerPtr listener, MessagePtr message)
{
    received_message = message;
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

    ResultMatch(ProcessPtr, Scheduler__exit(scheduler, proc, 10))
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_NOT_NULL(received_message);
    TEST_ASSERT_EQUAL(SOURCE_PROCESS, received_message->source);
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, received_message->source_code);

    Payload payload = Unwrap(Payload, received_message->payload, (Payload){});
    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_ANYTHING, payload.type);
    TEST_ASSERT_EQUAL(proc->pid, payload.anything);

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
    ResultMatch(ProcessPtr, Scheduler__park(scheduler, parent, &kernel_context, Some(Continuation, continuation), PARK_WAIT_CHILD))
        IsError(code, message, TEST_FAIL())
    End
    Scheduler__preempt(scheduler, cpu_id, context);

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(child, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->state);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__exit(scheduler, child, 10))
        IsError(code, message, TEST_FAIL());
        IsSuccess(exited,
        {
            TEST_ASSERT_EQUAL(child, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        })
    End
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);

    Anything return_code = ResultUnwrap(Anything, deferred_syscall_result(), (Anything){});
    TEST_ASSERT_EQUAL(10, return_code); // return code should have been set for switching context

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(parent, current);
            TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, current->state);
        })
    End
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
    ResultMatch(ProcessPtr, Scheduler__park(scheduler, parent, &kernel_context, Some(Continuation, continuation), PARK_WAIT_CHILD))
        IsError(code, message, TEST_FAIL())
    End
    Scheduler__preempt(scheduler, cpu_id, context);

    ProcessAbort abort = (ProcessAbort) {
        .reason = PROCESS_ABORT_DATA_ABORT,
        .pc = 0x400100,
        .fault_address = 0x0,
        .exception_code = 0x24,
        .syndrome = 0x96000004,
        .cpu = cpu_id,
    };

    ResultMatch(ProcessPtr, Scheduler__abort_process(scheduler, kernel->io_subsystem, child, abort))
        IsError(code, message, TEST_FAIL())
        IsSuccess(aborted,
        {
            TEST_ASSERT_EQUAL(child, aborted);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, aborted->state);
        })
    End

    Match(ProcessAbort, child->abort)
        IsNone(TEST_FAIL())
        IsSome(recorded,
        {
            TEST_ASSERT_EQUAL(PROCESS_ABORT_DATA_ABORT, recorded.reason);
            TEST_ASSERT_EQUAL_HEX64(0x400100, recorded.pc);
            TEST_ASSERT_EQUAL_HEX64(0x96000004, recorded.syndrome);
        })
    End

    TEST_ASSERT_TRUE(abort_stderr_written);
    TEST_ASSERT_TRUE(abort_stderr_size > 0);
    TEST_ASSERT_NOT_NULL(strstr(abort_stderr_buffer, "process fault: data abort"));
    TEST_ASSERT_NOT_NULL(strstr(abort_stderr_buffer, "pc=0x400100"));

    Anything return_code = ResultUnwrap(Anything, deferred_syscall_result(), (Anything){});
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

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, other_proc);
        })
    End


    ResultMatch(ProcessPtr, Scheduler__exit(scheduler, proc, 10))
        IsError(code, message, TEST_FAIL());
        IsSuccess(exited,
        {
            TEST_ASSERT_EQUAL(proc, exited);
            TEST_ASSERT_EQUAL(PROC_STATE_DEAD, exited->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(other_proc, current);
        })
    End

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

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, proc);
        })
    End


    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(idle, current);
        })
    End

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

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, proc);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, Some(ReturnCode, 11)))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->state);
        })
    End
}

void test_scheduler_unpark_gives_error_and_doesnt_unpark_if_reasons_dont_match(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    ProcessContextPtr context = (ProcessContextPtr) 0xFFEE00FF; // whatever as long as we dont deref

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, proc);
    Scheduler__start(scheduler, cpu_id);
    Scheduler__preempt(scheduler, cpu_id, context);

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, proc);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_CHILD, Some(ReturnCode, 11)))
        IsSuccess(parked, TEST_FAIL())
    End
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

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, running_proc);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    ResultMatch(ProcessPtr, Scheduler__unpark(scheduler, proc, PARK_WAIT_IO, Some(ReturnCode, 11)))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, parked->state);
        })
    End

    // it should cause a context switch as CPU is currently idle
    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(running_proc, current);
        })
    End
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

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(current, running);
        })
    End


    ResultMatch(ProcessPtr, Scheduler__park(scheduler, proc, &kernel_context, Some(Continuation, continuation), PARK_WAIT_IO))
        IsError(code, message, TEST_FAIL());
        IsSuccess(parked,
        {
            TEST_ASSERT_EQUAL(proc, parked);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parked->state);
        })
    End

    Match(ProcessPtr, Scheduler__current_process(scheduler, cpu_id))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(running, current);
        })
    End

    ProcessTablePtr processes = Scheduler__all_processes(scheduler, cpu_id);

    TEST_ASSERT_EQUAL(2, ProcessTable__len(processes));
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

    Match(ProcessPtr, Scheduler__current_process(scheduler, scheduled_cpu))
        IsNone(TEST_FAIL());
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(dummy, current);
        });
    End
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

    Match(ProcessPtr, Scheduler__current_process(scheduler, dummy_cpu))
        IsSome(current,
        {
            TEST_ASSERT_EQUAL(proc_least_recently_run, current);
        });
    End
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

void test_service_wait_parks_service_process_when_no_event_is_available(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, service_proc->state);
    Match(ParkedKernelContextPtr, Process__top_parked_context(service_proc))
        IsNone(TEST_FAIL())
        IsSome(parked, TEST_ASSERT_EQUAL(PARK_WAIT_SERVICE_EVENT, parked->park_reason))
    End
}

void test_service_enqueue_wakes_parked_service_process_and_wait_returns_event(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    ServiceEvent irq_event = (ServiceEvent) {
        .type = SERVICE_EVENT_IRQ,
        .source = 32,
        .data = 99,
    };

    ResultMatch(ServiceCode, Scheduler__service_enqueue(scheduler, service_proc, irq_event))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->state);

    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(32, event.source);
            TEST_ASSERT_EQUAL(99, event.data);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_scheduler_irq_event_wakes_parked_service_process(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 39;

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, service_proc->state);

    ResultMatch(ServiceCode, Scheduler__send_irq_event(scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, service_proc->state);

    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_EQUAL(0, event.data);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_scheduler_subscribed_irq_is_sent_to_service_process(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceCode, Scheduler__send_irq_event(scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));
    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(irq, event.source);
            TEST_ASSERT_EQUAL(0, event.data);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_scheduler_subscribed_irq_coalesces_while_pending_until_process_completes(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceCode, Scheduler__subscribe_irq(scheduler, service_proc, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceCode, Scheduler__send_irq_event(scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceCode, Scheduler__send_irq_event(scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&service_proc->service_events));
    TEST_ASSERT_EQUAL(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    ResultMatch(ServiceCode, Scheduler__complete_pending_irqs_for_process(scheduler, service_proc))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(irq, Arch__unmasked_irq());
    TEST_ASSERT_EQUAL(1, Arch__unmask_count());

    ResultMatch(ServiceCode, Scheduler__send_irq_event(scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(2, ServiceEvents__len(&service_proc->service_events));
    TEST_ASSERT_EQUAL(2, Arch__mask_count());
}

void test_service_timer_tick_event_is_not_sent_before_interval(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceCode, Service__send_timer_tick_events(scheduler, SERVICE_TIMER_EVENT_INTERVAL - 1))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(0, ServiceEvents__len(&service_proc->service_events));
}

void test_service_timer_tick_event_is_sent_to_service_process_at_interval(void)
{
    ProcessPtr service_proc = dummy_process(100, cpu_id, PROC_STATE_READY);
    service_proc->process_class = PROCESS_CLASS_SERVICE;

    Scheduler__schedule(scheduler, service_proc);

    ResultMatch(ServiceCode, Service__send_timer_tick_events(scheduler, SERVICE_TIMER_EVENT_INTERVAL))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    KernelContext kernel_context = KernelContext__new(None(ProcessID), Some(SchedulerPtr, scheduler));
    ResultMatch(ServiceEvent, Scheduler__service_wait(scheduler, service_proc, &kernel_context, None(Continuation)))
        IsSuccess(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL(SERVICE_TIMER_TICK_SOURCE, event.source);
            TEST_ASSERT_EQUAL(SERVICE_TIMER_EVENT_INTERVAL, event.data);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_service_timer_tick_event_is_not_sent_to_normal_process(void)
{
    ProcessPtr proc = dummy_process(100, cpu_id, PROC_STATE_READY);

    Scheduler__schedule(scheduler, proc);

    ResultMatch(ServiceCode, Service__send_timer_tick_events(scheduler, SERVICE_TIMER_EVENT_INTERVAL))
        IsSuccess(code, TEST_ASSERT_EQUAL_INT(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(0, ServiceEvents__len(&proc->service_events));
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
        Message__heap_new_anything(SOURCE_IO, SUBSYS_IO_AVAILABLE, proc->pid);
    MessageBroker__deliver(kernel->message_broker, message_available);
    Scheduler__preempt(scheduler, cpu_id, context);

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, proc->state);
    TEST_ASSERT_EQUAL(&kernel_context, continuation_context->kernel_context);

}

#include "unity.h"

#include "../../../kernel/irq/irq_dispatcher.h"
#include "../../../kernel/service/service.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process.h"
#include "../stubs/cpu.h"

static KernelPtr kernel = NULL;
static bool kernel_consumer_called = false;
static IRQDispatchContext observed_context;
static KernelPtr observed_kernel = NULL;
extern uint64_t test_cpu_time_usec;

void Arch__reset_irq_tracking();
uint64_t Arch__enabled_irq();
uint64_t Arch__enable_count();
uint64_t Arch__masked_irq();
uint64_t Arch__mask_count();

static Result(IRQDispatchStatus) kernel_consumer(IRQDispatchContextPtr context, KernelPtr passed_kernel)
{
    kernel_consumer_called = true;
    observed_context = *context;
    observed_kernel = passed_kernel;
    return Success(IRQDispatchStatus, IRQ_DISPATCH_HANDLED);
}

void setUp(void)
{
    clear_panic_state();
    current_process_reset();
    reset_cpu_reschedule();
    Arch__reset_irq_tracking();
    kernel = init_test_kernel();
    kernel_consumer_called = false;
    test_cpu_time_usec = 1234;
    observed_context = (IRQDispatchContext){0};
    observed_kernel = NULL;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_irq_dispatcher_returns_unhandled_when_no_consumer_registered(void)
{
    IRQDispatchContext context = (IRQDispatchContext){
        .irq = 48,
        .cpu = 0,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, 48, &context),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_UNHANDLED, status);
        },
        error_test_fail()
    );
}

void test_irq_dispatcher_calls_registered_kernel_consumer(void)
{
    IRQDispatchContext context = (IRQDispatchContext){
        .irq = 27,
        .cpu = 2,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(
        IRQDispatcher__register_kernel(kernel->irq_dispatcher, 27, kernel_consumer, kernel),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, 27, &context),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_HANDLED, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_TRUE(kernel_consumer_called);
    TEST_ASSERT_EQUAL_UINT64(27, observed_context.irq);
    TEST_ASSERT_EQUAL_UINT64(2, observed_context.cpu);
    TEST_ASSERT_EQUAL_PTR(kernel, observed_kernel);
}

void test_irq_dispatcher_registers_service_consumer_enables_irq_and_queues_event(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    IRQDispatchContext context = (IRQDispatchContext){
        .irq = 48,
        .cpu = 0,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(
        IRQDispatcher__register_service(kernel->irq_dispatcher, 48, process),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(48, Arch__enabled_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());

    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, 48, &context),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_RESCHEDULE, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(48, Arch__masked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__mask_count());

    ServiceEvent event = (ServiceEvent){0};
    KernelContext kernel_context =
        KernelContext__new(&Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler));
    match_result(
        Scheduler__service_wait(kernel->scheduler, process, &kernel_context, &None(Continuation)),
        success(wait_event)
        {
            event = wait_event;
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
    TEST_ASSERT_EQUAL_UINT64(48, event.source);
}

void test_irq_dispatcher_rejects_duplicate_kernel_registration(void)
{
    IRQDispatchContext context = (IRQDispatchContext){
        .irq = 27,
        .cpu = 0,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(
        IRQDispatcher__register_kernel(kernel->irq_dispatcher, 27, kernel_consumer, kernel),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    match_result(
        IRQDispatcher__register_kernel(kernel->irq_dispatcher, 27, kernel_consumer, kernel),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IRQ_ALREADY_REGISTERED, code);
        }
    );

    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, 27, &context),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_HANDLED, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_PTR(kernel, observed_kernel);
}

void test_irq_dispatcher_rejects_duplicate_service_registration(void)
{
    ProcessPtr first_process = dummy_process(1, 0, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, 0, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;

    match_result(
        IRQDispatcher__register_service(kernel->irq_dispatcher, 48, first_process),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    match_result(
        IRQDispatcher__register_service(kernel->irq_dispatcher, 48, second_process),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IRQ_ALREADY_REGISTERED, code);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());
}

void test_irq_dispatcher_rejects_service_registration_when_kernel_consumer_exists(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    match_result(
        IRQDispatcher__register_kernel(kernel->irq_dispatcher, 48, kernel_consumer, kernel),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    match_result(
        IRQDispatcher__register_service(kernel->irq_dispatcher, 48, process),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IRQ_ALREADY_REGISTERED, code);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(0, Arch__enable_count());
}

void test_irq_dispatcher_dispose_process_resources_unregisters_service_consumer(void)
{
    ProcessPtr first_process = dummy_process(1, 0, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, 0, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 48;

    match_result(
        IRQDispatcher__register_service(kernel->irq_dispatcher, irq, first_process),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    match_result(
        IRQDispatcher__dispose_process_resources(kernel->irq_dispatcher, first_process),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__mask_count());

    match_result(
        IRQDispatcher__register_service(kernel->irq_dispatcher, irq, second_process),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(2, Arch__enable_count());
}

void test_irq_dispatcher_publishes_managed_irq_to_manager_queue(void)
{
    ProcessPtr manager = dummy_process(1, 0, PROC_STATE_READY);
    manager->process_class = PROCESS_CLASS_SERVICE;
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, manager, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(IRQDispatcher__has_pending_notifications(kernel->irq_dispatcher, manager));

    match_result(
        IRQDispatcher__register_manager(kernel->irq_dispatcher, manager),
        success(address)
        {
            TEST_ASSERT_EQUAL_UINT64(KERNEL_NOTIFICATION_QUEUE_ADDRESS, address);
        },
        error_test_fail()
    );
    match_result(
        IRQDispatcher__register_managed_irq(kernel->irq_dispatcher, 48, manager),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_REGISTERED, status);
        },
        error_test_fail()
    );
    match_result(
        Scheduler__queue_wait(kernel->scheduler, manager,
                              IpcEvent__generation(IpcDoorbell__event(k_v_ptr(
                                  unwrap_result(Process__doorbell(manager), NULL_USER_ADDRESS), IpcDoorbell))),
                              QUEUE_WAIT_INDEFINITE_TICKS),
        success(status)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, status);
        },
        error_test_fail()
    );

    IRQDispatchContext context = IRQDispatchContext__new(48, 0, false, NULL);
    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, 48, &context),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_RESCHEDULE, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(48, Arch__masked_irq());
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, manager->state);
    TEST_ASSERT_TRUE(IRQDispatcher__has_pending_notifications(kernel->irq_dispatcher, manager));
    ProcessPtr other = dummy_process(2, 0, PROC_STATE_READY);
    TEST_ASSERT_FALSE(IRQDispatcher__has_pending_notifications(kernel->irq_dispatcher, other));
    match_optional(
        kernel->irq_dispatcher->manager_queue_memory,
        some(memory)
        {
            TEST_ASSERT_TRUE(memory.write);
            KernelNotificationQueuePtr queue = k_v_ptr(memory, KernelNotificationQueue);
            match_optional(
                KernelNotificationQueue__pop(queue),
                some(event)
                {
                    TEST_ASSERT_EQUAL_UINT64(48, event.id);
                    TEST_ASSERT_EQUAL_UINT64(1, event.sequence);
                    TEST_ASSERT_EQUAL_UINT64(test_cpu_time_usec, event.occurred_at_usec);
                },
                none_test_fail()
            );
            TEST_ASSERT_FALSE(IRQDispatcher__has_pending_notifications(kernel->irq_dispatcher, manager));
        },
        none_test_fail()
    );
}

void test_irq_dispatcher_pokes_remote_cpu_for_parked_interrupt_manager(void)
{
    CPUID manager_cpu = 1;
    ProcessPtr manager = dummy_process(1, manager_cpu, PROC_STATE_READY);
    manager->process_class = PROCESS_CLASS_REALTIME_SERVICE;
    Scheduler__start(kernel->scheduler, manager_cpu);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, manager, manager_cpu),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(IRQDispatcher__register_manager(kernel->irq_dispatcher, manager),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(IRQDispatcher__register_managed_irq(kernel->irq_dispatcher, 48, manager),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(
        Scheduler__queue_wait(kernel->scheduler, manager,
                              IpcEvent__generation(IpcDoorbell__event(k_v_ptr(
                                  unwrap_result(Process__doorbell(manager), NULL_USER_ADDRESS), IpcDoorbell))),
                              QUEUE_WAIT_INDEFINITE_TICKS),
        success(status)
        {
            TEST_ASSERT_EQUAL(QUEUE_WAIT_PARKED, status);
        },
        error_test_fail()
    );

    IRQDispatchContext context = IRQDispatchContext__new(48, 0, false, NULL);
    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, 48, &context),
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_HANDLED, status);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, manager->state);
    TEST_ASSERT_EQUAL_UINT64(1, CPUStub__reschedule_count());
    TEST_ASSERT_EQUAL_UINT64(manager_cpu, CPUStub__rescheduled_cpu());
}

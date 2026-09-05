#include "unity.h"

#include "../../../kernel/irq/irq_dispatcher.h"
#include "../../../kernel/service/service.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process.h"

static KernelPtr kernel = NULL;
static bool kernel_consumer_called = false;
static IRQDispatchContext observed_context;
static void * observed_data = NULL;

void Arch__reset_irq_tracking();
uint64_t Arch__enabled_irq();
uint64_t Arch__enable_count();
uint64_t Arch__masked_irq();
uint64_t Arch__mask_count();

static Result(IRQDispatchStatus) kernel_consumer(IRQDispatchContextPtr context, void * data)
{
    kernel_consumer_called = true;
    observed_context = *context;
    observed_data = data;
    return Success(IRQDispatchStatus, IRQ_DISPATCH_HANDLED);
}

void setUp(void)
{
    clear_panic_state();
    current_process_reset();
    Arch__reset_irq_tracking();
    kernel = init_test_kernel_without_filesystem();
    kernel_consumer_called = false;
    observed_context = (IRQDispatchContext) { 0 };
    observed_data = NULL;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_irq_dispatcher_returns_unhandled_when_no_consumer_registered(void)
{
    IRQDispatchContext context = (IRQDispatchContext) {
        .irq = 48,
        .cpu = 0,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(IRQDispatchStatus, IRQDispatcher__dispatch(kernel->irq_dispatcher, 48, &context), success(status) { TEST_ASSERT_EQUAL(IRQ_DISPATCH_UNHANDLED, status); }, error_test_fail());
}

void test_irq_dispatcher_calls_registered_kernel_consumer(void)
{
    uint64_t data = 1234;
    IRQDispatchContext context = (IRQDispatchContext) {
        .irq = 27,
        .cpu = 2,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(IRQRegistrationStatus, IRQDispatcher__register_kernel(
        kernel->irq_dispatcher, 27, kernel_consumer, &data
    ), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    match_result(IRQDispatchStatus, IRQDispatcher__dispatch(kernel->irq_dispatcher, 27, &context), success(status) { TEST_ASSERT_EQUAL(IRQ_DISPATCH_HANDLED, status); }, error_test_fail());

    TEST_ASSERT_TRUE(kernel_consumer_called);
    TEST_ASSERT_EQUAL_UINT64(27, observed_context.irq);
    TEST_ASSERT_EQUAL_UINT64(2, observed_context.cpu);
    TEST_ASSERT_EQUAL_PTR(&data, observed_data);
}

void test_irq_dispatcher_registers_service_consumer_enables_irq_and_queues_event(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    IRQDispatchContext context = (IRQDispatchContext) {
        .irq = 48,
        .cpu = 0,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, 48, process), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(48, Arch__enabled_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());

    match_result(IRQDispatchStatus, IRQDispatcher__dispatch(kernel->irq_dispatcher, 48, &context), success(status) { TEST_ASSERT_EQUAL(IRQ_DISPATCH_RESCHEDULE, status); }, error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(48, Arch__masked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__mask_count());

    ServiceEvent event = (ServiceEvent) { 0 };
    KernelContext kernel_context = KernelContext__new(&Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler));
    match_result(ServiceEvent, Scheduler__service_wait(kernel->scheduler, process, &kernel_context, &None(Continuation)), success(wait_event) { event = wait_event; }, error_test_fail());

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
    TEST_ASSERT_EQUAL_UINT64(48, event.source);
}

void test_irq_dispatcher_rejects_duplicate_kernel_registration(void)
{
    uint64_t first_data = 1;
    uint64_t second_data = 2;
    IRQDispatchContext context = (IRQDispatchContext) {
        .irq = 27,
        .cpu = 0,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(IRQRegistrationStatus, IRQDispatcher__register_kernel(
        kernel->irq_dispatcher, 27, kernel_consumer, &first_data
    ), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    match_result(IRQRegistrationStatus, IRQDispatcher__register_kernel(
        kernel->irq_dispatcher, 27, kernel_consumer, &second_data
    ), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(IRQ_ALREADY_REGISTERED, code); });

    match_result(IRQDispatchStatus, IRQDispatcher__dispatch(kernel->irq_dispatcher, 27, &context), success(status) { TEST_ASSERT_EQUAL(IRQ_DISPATCH_HANDLED, status); }, error_test_fail());

    TEST_ASSERT_EQUAL_PTR(&first_data, observed_data);
}

void test_irq_dispatcher_rejects_duplicate_service_registration(void)
{
    ProcessPtr first_process = dummy_process(1, 0, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, 0, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;

    match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, 48, first_process), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, 48, second_process), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(IRQ_ALREADY_REGISTERED, code); });

    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());
}

void test_irq_dispatcher_rejects_service_registration_when_kernel_consumer_exists(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    match_result(IRQRegistrationStatus, IRQDispatcher__register_kernel(
        kernel->irq_dispatcher, 48, kernel_consumer, NULL
    ), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, 48, process), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(IRQ_ALREADY_REGISTERED, code); });

    TEST_ASSERT_EQUAL_UINT64(0, Arch__enable_count());
}

void test_irq_dispatcher_dispose_process_resources_unregisters_service_consumer(void)
{
    ProcessPtr first_process = dummy_process(1, 0, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, 0, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 48;

    match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, irq, first_process), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    match_result(IRQRegistrationStatus, IRQDispatcher__dispose_process_resources(kernel->irq_dispatcher, first_process), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(irq, Arch__masked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__mask_count());

    match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, irq, second_process), success(status) { TEST_ASSERT_EQUAL(IRQ_REGISTERED, status); }, error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(2, Arch__enable_count());
}

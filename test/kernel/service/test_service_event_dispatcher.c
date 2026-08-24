#include "unity.h"

#include "../../../kernel/process.h"
#include "../../../kernel/service/event_dispatcher.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"

static InterruptControllerInfo interrupt_controller = { 0 };

void Arch__reset_irq_tracking();
uint64_t Arch__masked_irq();
uint64_t Arch__unmasked_irq();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();

void setUp(void)
{
    clear_panic_state();
    Arch__reset_irq_tracking();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_service_event_dispatcher_dispatches_irq_and_completes_active_irq(void)
{
    ServiceEventDispatcher dispatcher = ServiceEventDispatcher__new();
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    match_result(ServiceCode, ServiceEventDispatcher__subscribe_irq(&dispatcher, process, 48), success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); }, error_test_fail());

    match_result(ServiceEventDispatch, ServiceEventDispatcher__dispatch_irq(&dispatcher, &interrupt_controller, 48), success(dispatch) {
            match_optional(ProcessPtr, dispatch.process, some(target) { TEST_ASSERT_EQUAL_PTR(process, target); }, none_test_fail());
        }, error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(48, Arch__masked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&process->service.events));

    match_result(ServiceEvent, ServiceEventDispatcher__take_next(&dispatcher, process), success(event) {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL_UINT64(48, event.source);
        }, error_test_fail());

    match_result(ServiceCode, ServiceEventDispatcher__complete_active_irq(&dispatcher, &interrupt_controller, process), success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL_UINT64(48, Arch__unmasked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__unmask_count());
}

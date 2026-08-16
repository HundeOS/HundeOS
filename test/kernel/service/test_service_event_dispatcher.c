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

    ResultMatch(ServiceCode, ServiceEventDispatcher__subscribe_irq(&dispatcher, process, 48))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceEventDispatch, ServiceEventDispatcher__dispatch_irq(&dispatcher, &interrupt_controller, 48))
        IsSuccess(dispatch,
        {
            Match(ProcessPtr, dispatch.process)
                IsSome(target, TEST_ASSERT_EQUAL_PTR(process, target))
                IsNone(TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL_UINT64(48, Arch__masked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__mask_count());
    TEST_ASSERT_EQUAL(1, ServiceEvents__len(&process->service.events));

    ResultMatch(ServiceEvent, ServiceEventDispatcher__take_next(&dispatcher, process))
        IsSuccess(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
            TEST_ASSERT_EQUAL_UINT64(48, event.source);
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceCode, ServiceEventDispatcher__complete_active_irq(&dispatcher, &interrupt_controller, process))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL_UINT64(48, Arch__unmasked_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__unmask_count());
}

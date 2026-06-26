#include "unity.h"

#include "../../../kernel/syscall/service_handler.h"
#include "../../../kernel/service/service.h"
#include "../../../kernel/arch/process.h"
#include "../../../common_includes/syscalls/service.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process.h"

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;

void Arch__reset_irq_tracking();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();

void setUp(void)
{
    clear_panic_state();
    clear_deferred_syscall_result();
    current_process_reset();
    Arch__reset_irq_tracking();
    kernel = init_test_kernel_without_filesystem();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_service_handler_wait_copies_queued_event(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    ServiceEvent queued = (ServiceEvent) {
        .type = SERVICE_EVENT_IRQ,
        .source = 44,
        .data = 99,
    };
    ServiceEvent target = (ServiceEvent) { 0 };

    ResultMatch(ServiceCode, Service__enqueue(kernel->scheduler, process, queued))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
            TEST_ASSERT_EQUAL(44, target.source);
            TEST_ASSERT_EQUAL(99, target.data);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_service_handler_wait_parks_and_completes_when_event_arrives(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceEvent target = (ServiceEvent) { 0 };

    Scheduler__schedule(kernel->scheduler, process);

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);

    ServiceEvent queued = (ServiceEvent) {
        .type = SERVICE_EVENT_MESSAGE,
        .source = 7,
        .data = 11,
    };

    ResultMatch(ServiceCode, Service__enqueue(kernel->scheduler, process, queued))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, process->state);

    Process__context_switch(process);

    ResultMatch(Anything, deferred_syscall_result())
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(SERVICE_EVENT_MESSAGE, target.type);
    TEST_ASSERT_EQUAL(7, target.source);
    TEST_ASSERT_EQUAL(11, target.data);
    TEST_ASSERT_NULL(process->service_wait_target);
}

void test_service_handler_wait_only_completes_pending_irq_when_it_parks(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;
    ServiceEvent target = (ServiceEvent) { 0 };

    Scheduler__schedule(kernel->scheduler, process);

    ResultMatch(ServiceCode, Scheduler__subscribe_irq(kernel->scheduler, process, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceCode, Scheduler__send_irq_event(kernel->scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
    TEST_ASSERT_EQUAL(irq, target.source);
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    TEST_ASSERT_EQUAL(1, Arch__unmask_count());
}

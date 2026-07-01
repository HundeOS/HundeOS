#include "unity.h"

#include "../../../kernel/syscall/process_handler.h"
#include "../../../common_includes/syscalls/process.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;

void setUp(void)
{
    clear_panic_state();
    kernel = init_test_kernel_without_filesystem();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_process_list_handler_returns_scheduled_processes(void)
{
    ProcessPtr init = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr driver = dummy_process(2, cpu, PROC_STATE_READY);
    driver->process_class = PROCESS_CLASS_SERVICE;

    Scheduler__schedule(kernel->scheduler, init);
    Scheduler__schedule(kernel->scheduler, driver);
    KernelContext kernel_context = KernelContext__new(Some(ProcessID, driver->pid), Some(SchedulerPtr, kernel->scheduler));
    Scheduler__park(kernel->scheduler, driver, &kernel_context, None(Continuation), PARK_WAIT_SERVICE_EVENT);

    ProcessInfo entries[4];
    ProcessListRequest request = (ProcessListRequest) {
        .entries = entries,
        .max_entries = 4,
    };

    ResultMatch(SystemCallResult, ProcessList__handler(
        kernel, Some(ProcessPtr, init), (Anything) &request, 0, 0, 0, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_UINT64(3, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_RUNNING", entries[0].state);
            TEST_ASSERT_EQUAL_STRING("-", entries[0].wait_reason);
            TEST_ASSERT_EQUAL_STRING("normal", entries[0].class_name);
            TEST_ASSERT_EQUAL_UINT64(init->pid, entries[1].pid);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_READY", entries[1].state);
            TEST_ASSERT_EQUAL_STRING("-", entries[1].wait_reason);
            TEST_ASSERT_EQUAL_STRING("normal", entries[1].class_name);
            TEST_ASSERT_EQUAL_UINT64(driver->pid, entries[2].pid);
            TEST_ASSERT_EQUAL_STRING("PROC_STATE_WAITING", entries[2].state);
            TEST_ASSERT_EQUAL_STRING("PARK_WAIT_SERVICE_EVENT", entries[2].wait_reason);
            TEST_ASSERT_EQUAL_STRING("service", entries[2].class_name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_process_list_handler_respects_entry_limit(void)
{
    ProcessPtr first = dummy_process(1, cpu, PROC_STATE_READY);
    ProcessPtr second = dummy_process(2, cpu, PROC_STATE_READY);

    Scheduler__schedule(kernel->scheduler, first);
    Scheduler__schedule(kernel->scheduler, second);

    ProcessInfo entries[1];
    ProcessListRequest request = (ProcessListRequest) {
        .entries = entries,
        .max_entries = 1,
    };

    ResultMatch(SystemCallResult, ProcessList__handler(
        kernel, Some(ProcessPtr, first), (Anything) &request, 0, 0, 0, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_UINT64(1, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_process_list_handler_rejects_missing_request(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);

    ResultMatch(SystemCallResult, ProcessList__handler(
        kernel, Some(ProcessPtr, process), 0, 0, 0, 0, 0
    ))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(PROCESS_LIST_INVALID_ARGUMENT, code))
    End
}

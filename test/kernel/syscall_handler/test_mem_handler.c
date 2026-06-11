#include "unity.h"

#include "../../../kernel/syscall/mem_handler.h"
#include "../../../common_includes/syscalls/mem.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_mem_handler_returns_no_process_without_current_process(void)
{
    ResultMatch(SystemCallResult, Mem__handler(NULL, None(ProcessPtr), MEM_ALLOC_PAGES, PAGE_SIZE, 0, 0, 0))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MEM_NO_PROCESS, code))
    End
}

void test_mem_handler_rejects_zero_size_allocations(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    ResultMatch(SystemCallResult, Mem__handler(NULL, Some(ProcessPtr, process), MEM_ALLOC_PAGES, 0, 0, 0, 0))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MEM_INVALID_SIZE, code))
    End
}

void test_mem_handler_rejects_unsupported_operations(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    ResultMatch(SystemCallResult, Mem__handler(NULL, Some(ProcessPtr, process), 100, 0, 0, 0, 0))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MEM_OPERATION_NOT_SUPPORTED, code))
    End
}

void test_mem_handler_maps_static_free_to_invalid_address(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    ResultMatch(SystemCallResult, Mem__handler(NULL, Some(ProcessPtr, process), MEM_FREE_PAGES, BASE_STACK, 0, 0, 0))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MEM_INVALID_ADDRESS, code))
    End
}

void test_mem_handler_sbrk_returns_old_break(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    ResultMatch(SystemCallResult, Mem__handler(NULL, Some(ProcessPtr, process), MEM_SBRK, 100, 0, 0, 0))
        IsSuccess(old_break, TEST_ASSERT_EQUAL(USER_HEAP_BASE, old_break))
        IsError(code, message, TEST_FAIL())
    End
}

void test_mem_handler_sbrk_maps_invalid_size_errors(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    ResultMatch(SystemCallResult, Mem__handler(NULL, Some(ProcessPtr, process), MEM_SBRK, -1, 0, 0, 0))
        IsSuccess(old_break, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(MEM_INVALID_SIZE, code))
    End
}

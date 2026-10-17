#include <string.h>

#include "unity.h"

#include "../../../kernel/trace_buffer.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_kernel_trace_buffer_snapshots_newest_bytes_without_draining(void)
{
    KernelTraceBuffer buffer = KERNEL_TRACE_BUFFER_INIT;
    uint8_t first[5] = {0};
    uint8_t second[5] = {0};

    KernelTraceBuffer__append(&buffer, "hello world");
    TEST_ASSERT_EQUAL_UINT64(5, unwrap_result(KernelTraceBuffer__snapshot(&buffer, first, sizeof(first)), 0));
    TEST_ASSERT_EQUAL_UINT64(5, unwrap_result(KernelTraceBuffer__snapshot(&buffer, second, sizeof(second)), 0));
    TEST_ASSERT_EQUAL_MEMORY("world", first, sizeof(first));
    TEST_ASSERT_EQUAL_MEMORY(first, second, sizeof(first));
}

void test_kernel_trace_buffer_overwrites_oldest_bytes(void)
{
    KernelTraceBuffer buffer = KERNEL_TRACE_BUFFER_INIT;
    char initial[KERNEL_TRACE_CAPACITY + 1U];
    uint8_t snapshot[KERNEL_TRACE_CAPACITY];
    memset(initial, 'a', KERNEL_TRACE_CAPACITY);
    initial[KERNEL_TRACE_CAPACITY] = '\0';

    KernelTraceBuffer__append(&buffer, initial);
    KernelTraceBuffer__append(&buffer, "bc");

    TEST_ASSERT_EQUAL_UINT64(KERNEL_TRACE_CAPACITY,
                             unwrap_result(KernelTraceBuffer__snapshot(&buffer, snapshot, sizeof(snapshot)), 0));
    TEST_ASSERT_EQUAL_UINT8('a', snapshot[0]);
    TEST_ASSERT_EQUAL_UINT8('a', snapshot[KERNEL_TRACE_CAPACITY - 3U]);
    TEST_ASSERT_EQUAL_UINT8('b', snapshot[KERNEL_TRACE_CAPACITY - 2U]);
    TEST_ASSERT_EQUAL_UINT8('c', snapshot[KERNEL_TRACE_CAPACITY - 1U]);
}

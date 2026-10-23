#include "../../../hundetest.h"
#include "unity.h"
#include <string.h>
#include "../../../../user/lib/hunde/native.h"
#include "sys/process.h"
#include "sys/system_info.h"
#include "sys/mem.h"

static ProcessRegistry registry;
static bool fail_mapping;
static bool fail_trace;
static bool fail_allocation;
static bool fail_release;
static size_t allocations;
static size_t releases;
static size_t trace_calls;
static KernelTraceSize trace_size;
static uint8_t trace_storage[KERNEL_TRACE_CAPACITY];

Result(Anything) mem_alloc_pages_result(size_t size)
{
    allocations++;
    TEST_ASSERT_GREATER_THAN_UINT64(0, size);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(KERNEL_TRACE_CAPACITY, size);
    if (fail_allocation)
    {
        return Error(Anything, bstr("Allocation failed"), MEM_ALLOCATION_FAILED);
    }
    return Success(Anything, (Anything)trace_storage);
}

MemStatus mem_free_pages(void * address)
{
    TEST_ASSERT_EQUAL_PTR(trace_storage, address);
    releases++;
    return fail_release ? MEM_FREE_FAILED : MEM_OK;
}

Result(ConstProcessRegistryPtr) process_registry_map(void)
{
    if (fail_mapping)
    {
        return Error(ConstProcessRegistryPtr, bstr("Mapping failed"), 1);
    }
    return Success(ConstProcessRegistryPtr, &registry);
}

Result(KernelTraceSize) KernelTrace__load(uint8_t * destination, size_t capacity)
{
    trace_calls++;
    TEST_ASSERT_EQUAL_PTR(trace_storage, destination);
    if (fail_trace)
    {
        return Error(KernelTraceSize, bstr("Trace failed"), 1);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(5, capacity);
    memcpy(destination, "trace", 5);
    return Success(KernelTraceSize, trace_size);
}

void setUp(void)
{
    memset(&registry, 0, sizeof(registry));
    registry.version = PROCESS_REGISTRY_VERSION;
    fail_mapping = fail_trace = false;
    fail_allocation = fail_release = false;
    allocations = releases = trace_calls = 0;
    trace_size = 5;
    ProcessRecord record = {
        .pid = 42,
        .cpu = 3,
        .sampled_at_usec = 123456,
        .name = bstr("test-process"),
        .state = PROC_STATE_WAITING,
        .wait_reason = Some(ParkReason, PARK_WAIT_IO),
        .process_class = PROCESS_CLASS_NORMAL,
        .credentials = ProcessCredentials__new(1ULL << 40, 1ULL << 41),
        .cpu_usage = {.total_usec = 104900, .scheduled_usec = 120000},
        .queue_wait_usage = {.parks = 5},
        .memory = Some(ProcessMemoryUsage, ((ProcessMemoryUsage){.private_bytes = 8192})),
        .io_diagnostics = Some(IODiagnosticsSnapshot,
                               ((IODiagnosticsSnapshot){.block_cache_hits = 17, .block_device = {.p99_usec = 999}}))};
    match_result(ProcessRegistry__set(&registry, 3, 0, &record),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(ProcessRegistry__set_count(&registry, 3, 1),
        success_do_nothing(),
        error_test_fail()
    );
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT64(fail_allocation ? 0 : allocations, releases);
}

void test_process_adapter_copies_one_consistent_record_with_full_width_credentials(void)
{
    HundeProcessResult result = hunde_rust_process(42, 0);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL(1, result.found);
    TEST_ASSERT_EQUAL_UINT64(42, result.snapshot.pid);
    TEST_ASSERT_EQUAL_UINT64(3, result.snapshot.cpu);
    TEST_ASSERT_EQUAL_UINT64(123456, result.snapshot.sampled_at_usec);
    TEST_ASSERT_EQUAL_UINT64(1ULL << 40, result.snapshot.user_id);
    TEST_ASSERT_EQUAL_UINT64(1ULL << 41, result.snapshot.group_id);
    TEST_ASSERT_EQUAL_UINT64(12, result.snapshot.name.length);
    TEST_ASSERT_EQUAL_MEMORY("test-process", result.snapshot.name.bytes, 12);
    TEST_ASSERT_EQUAL_UINT64(104900, result.snapshot.cpu_usage.total_usec);
    TEST_ASSERT_EQUAL_UINT64(120000, result.snapshot.cpu_usage.scheduled_usec);
    TEST_ASSERT_EQUAL_UINT64(5, result.snapshot.queue_wait.parks);
    TEST_ASSERT_EQUAL_UINT64(1, result.snapshot.has_memory);
    TEST_ASSERT_EQUAL_UINT64(8192, result.snapshot.memory.private_bytes);
    TEST_ASSERT_EQUAL_UINT64(1, result.snapshot.has_diagnostics);
    TEST_ASSERT_EQUAL_UINT64(17, result.snapshot.diagnostics.block_cache_hits);
    TEST_ASSERT_EQUAL_UINT64(999, result.snapshot.diagnostics.block_device.p99_usec);
}

void test_index_missing_and_mapping_failures_are_distinct(void)
{
    HundeProcessResult first = hunde_rust_process(0, 1);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, first.kind);
    TEST_ASSERT_EQUAL_UINT64(42, first.snapshot.pid);
    HundeProcessResult missing = hunde_rust_process(1, 1);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, missing.kind);
    TEST_ASSERT_EQUAL(0, missing.found);
    TEST_ASSERT_EQUAL(0, hunde_rust_process(99, 0).found);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, hunde_rust_process(0, 2).error.code);
    fail_mapping = true;
    HundeProcessResult failure = hunde_rust_process(0, 1);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, failure.kind);
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, failure.error.code);
}

void test_trace_adapter_reports_size_and_failures_without_exposing_pointers(void)
{
    uint8_t bytes[16];
    HundeTraceResult trace = hunde_rust_kernel_trace(bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, trace.kind);
    TEST_ASSERT_EQUAL_UINT64(5, trace.size);
    TEST_ASSERT_EQUAL_MEMORY("trace", bytes, 5);
    TEST_ASSERT_EQUAL_UINT64(1, allocations);
    TEST_ASSERT_EQUAL_UINT64(1, releases);
    fail_trace = true;
    trace = hunde_rust_kernel_trace(bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, trace.kind);
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, trace.error.code);
    TEST_ASSERT_EQUAL_UINT64(2, releases);
}

void test_trace_allocation_failure_does_not_call_kernel_or_release_unallocated_memory(void)
{
    uint8_t bytes[16] = {0x55};
    fail_allocation = true;
    HundeTraceResult trace = hunde_rust_kernel_trace(bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, trace.kind);
    TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, trace.error.code);
    TEST_ASSERT_EQUAL_UINT64(1, allocations);
    TEST_ASSERT_EQUAL_UINT64(0, trace_calls);
    TEST_ASSERT_EQUAL_UINT64(0, releases);
    TEST_ASSERT_EQUAL_HEX8(0x55, bytes[0]);
}

void test_trace_rejects_invalid_capacity_without_allocating(void)
{
    uint8_t bytes[16];
    HundeTraceResult empty = hunde_rust_kernel_trace(bytes, 0);
    HundeTraceResult oversized = hunde_rust_kernel_trace(bytes, KERNEL_TRACE_CAPACITY + 1ULL);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, empty.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, empty.error.code);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, oversized.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, oversized.error.code);
    TEST_ASSERT_EQUAL_UINT64(0, allocations);
    TEST_ASSERT_EQUAL_UINT64(0, trace_calls);
}

void test_trace_rejects_oversized_kernel_result_and_still_releases_memory(void)
{
    uint8_t bytes[16];
    memset(bytes, 0x55, sizeof(bytes));
    trace_size = sizeof(bytes) + 1;
    HundeTraceResult trace = hunde_rust_kernel_trace(bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, trace.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, trace.error.code);
    TEST_ASSERT_EQUAL_UINT64(1, releases);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x55, bytes, sizeof(bytes));
}

void test_trace_release_failure_is_reported_after_success_or_kernel_failure(void)
{
    uint8_t bytes[16];
    fail_release = true;
    HundeTraceResult trace = hunde_rust_kernel_trace(bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, trace.kind);
    TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, trace.error.code);
    fail_trace = true;
    trace = hunde_rust_kernel_trace(bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, trace.kind);
    TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, trace.error.code);
    TEST_ASSERT_EQUAL_UINT64(2, releases);
}

void test_trace_handles_empty_and_full_snapshots_without_touching_bytes_outside_result(void)
{
    uint8_t bytes[KERNEL_TRACE_CAPACITY + 2];
    memset(bytes, 0x55, sizeof(bytes));
    trace_size = 0;
    HundeTraceResult empty = hunde_rust_kernel_trace(bytes + 1, KERNEL_TRACE_CAPACITY);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, empty.kind);
    TEST_ASSERT_EQUAL_UINT64(0, empty.size);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x55, bytes, sizeof(bytes));
    memset(trace_storage, 0x33, sizeof(trace_storage));
    trace_size = KERNEL_TRACE_CAPACITY;
    HundeTraceResult full = hunde_rust_kernel_trace(bytes + 1, KERNEL_TRACE_CAPACITY);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, full.kind);
    TEST_ASSERT_EQUAL_UINT64(KERNEL_TRACE_CAPACITY, full.size);
    TEST_ASSERT_EQUAL_HEX8(0x55, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, bytes[sizeof(bytes) - 1]);
    TEST_ASSERT_EQUAL_MEMORY("trace", bytes + 1, 5);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x33, bytes + 6, KERNEL_TRACE_CAPACITY - 5);
    TEST_ASSERT_EQUAL_UINT64(2, releases);
}

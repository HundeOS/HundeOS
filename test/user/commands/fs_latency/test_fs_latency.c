#include "unity.h"

#define main fs_latency_main_under_test
#include "../../../../../user/commands/fs-latency/main.c"
#undef main

static uint64_t opens;
static uint64_t reads;
static uint64_t closes;

void setUp(void)
{
    opens = 0;
    reads = 0;
    closes = 0;
}

void tearDown(void)
{
}

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    opens++;
    return 10;
}

ByteCount IO__read_bytes(IOHandle handle, char * data, size_t size)
{
    reads++;
    return (ByteCount)size;
}

IOStatus IO__close(IOHandle handle)
{
    closes++;
    return IO_OK;
}

IOSeekResult IO__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    return IOSeekResult__new(IO_OK, (uint64_t)offset);
}

Result(IOFileInfo) IO__file_info(ConstBStringPtr path)
{
    return Error(IOFileInfo, bstr("Trace fixture unavailable"), IO_NOT_OPEN);
}

void IO__print(ConstBStringPtr text)
{
}

TraceStatus IOTrace__drain(void)
{
    return TRACE_OK;
}

int IO__printf(ConstBStringPtr format, ...)
{
    return 0;
}

uint64_t process_current_pid(void)
{
    return 42;
}

Result(ConstProcessRegistryPtr) process_registry_map(void)
{
    return Success(ConstProcessRegistryPtr, (ConstProcessRegistryPtr)PROCESS_REGISTRY_VADDR);
}

Result(ProcessRecord)
    ProcessMetrics__snapshot_after(ConstProcessRegistryPtr source, uint64_t pid, uint64_t sampled_after_usec)
{
    return Success(ProcessRecord, ((ProcessRecord){.pid = pid, .sampled_at_usec = sampled_after_usec}));
}

Result(ProcessInfo) ProcessMetrics__info(uint64_t pid)
{
    return Success(ProcessInfo, ((ProcessInfo){.pid = pid}));
}

void test_fs_latency_runs_requested_samples(void)
{
    char * argv[] = {"fs-latency", "/usr/local/bin/cp", "4"};

    TEST_ASSERT_EQUAL_INT(0, fs_latency_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_UINT64(4, opens);
    TEST_ASSERT_EQUAL_UINT64(4, reads);
    TEST_ASSERT_EQUAL_UINT64(4, closes);
}

void test_fs_latency_rejects_invalid_sample_counts(void)
{
    char * zero[] = {"fs-latency", "/usr/local/bin/cp", "0"};
    char * excessive[] = {"fs-latency", "/usr/local/bin/cp", "1001"};
    char * invalid[] = {"fs-latency", "/usr/local/bin/cp", "many"};

    TEST_ASSERT_EQUAL_INT(1, fs_latency_main_under_test(3, zero));
    TEST_ASSERT_EQUAL_INT(1, fs_latency_main_under_test(3, excessive));
    TEST_ASSERT_EQUAL_INT(1, fs_latency_main_under_test(3, invalid));
    TEST_ASSERT_EQUAL_UINT64(0, opens);
}

void test_fs_latency_groups_hops_for_the_measured_origin_and_window(void)
{
    FSLatencyHops hops = FSLatencyHops__new();
    BString included = bstr("IO-HOP client=42 server=10 connection=1 generation=2 origin=42 parent_connection=0 "
                            "parent_generation=0 published_at_usec=110 consumed_at_usec=145 queue_usec=35");
    BString other_origin = bstr("IO-HOP client=43 server=10 connection=1 generation=2 origin=43 parent_connection=0 "
                                "parent_generation=0 published_at_usec=110 consumed_at_usec=145 queue_usec=35");
    BString outside_window = bstr("IO-HOP client=42 server=10 connection=1 generation=2 origin=42 parent_connection=0 "
                                  "parent_generation=0 published_at_usec=99 consumed_at_usec=145 queue_usec=46");

    FSLatencyHops__record_line(&hops, &included, 42, 100, 200);
    FSLatencyHops__record_line(&hops, &other_origin, 42, 100, 200);
    FSLatencyHops__record_line(&hops, &outside_window, 42, 100, 200);

    TEST_ASSERT_EQUAL_UINT64(1, FSLatencyHops__len(&hops));
    match_optional(
        FSLatencyHops__ptr_at(&hops, 0),
        some(hop)
        {
            TEST_ASSERT_EQUAL_UINT64(42, hop->client_pid);
            TEST_ASSERT_EQUAL_UINT64(10, hop->server_pid);
            TEST_ASSERT_EQUAL_UINT64(1, hop->samples);
            TEST_ASSERT_EQUAL_UINT64(39, IODiagnosticLatency__percentile(&hop->latency, 50));
        },
        none_test_fail()
    );
}

void test_fs_latency_correlates_parent_pickup_with_sequential_child_requests(void)
{
    FSLatencySpans spans = FSLatencySpans__new();
    BString parent = bstr("IO-LATENCY client=42 server=10 connection=1 generation=1 origin=42 parent_connection=0 "
                          "parent_generation=0 published_at_usec=100 received_at_usec=110 replied_at_usec=180 "
                          "completed_at_usec=190");
    BString first_child = bstr("IO-LATENCY client=10 server=9 connection=2 generation=1 origin=42 parent_connection=1 "
                               "parent_generation=1 published_at_usec=115 received_at_usec=120 replied_at_usec=130 "
                               "completed_at_usec=135");
    BString second_child = bstr("IO-LATENCY client=10 server=9 connection=2 generation=2 origin=42 parent_connection=1 "
                                "parent_generation=1 published_at_usec=140 received_at_usec=145 replied_at_usec=155 "
                                "completed_at_usec=160");

    FSLatencySpans__record_line(&spans, &parent, 42, 90, 200);
    FSLatencySpans__record_line(&spans, &first_child, 42, 90, 200);
    FSLatencySpans__record_line(&spans, &second_child, 42, 90, 200);
    FSLatencyChains chains = FSLatencySpans__chains(&spans);

    TEST_ASSERT_EQUAL_UINT64(1, FSLatencyChains__len(&chains));
    match_optional(
        FSLatencyChains__ptr_at(&chains, 0),
        some(chain)
        {
            TEST_ASSERT_EQUAL_UINT64(42, chain->parent_client_pid);
            TEST_ASSERT_EQUAL_UINT64(10, chain->parent_server_pid);
            TEST_ASSERT_EQUAL_UINT64(9, chain->child_server_pid);
            TEST_ASSERT_EQUAL_UINT64(2, chain->samples);
            TEST_ASSERT_EQUAL_UINT64(1, chain->resume_samples);
            TEST_ASSERT_EQUAL_UINT64(5, IODiagnosticLatency__percentile(&chain->pickup_to_emit, 50));
            TEST_ASSERT_EQUAL_UINT64(23, IODiagnosticLatency__percentile(&chain->downstream, 50));
            TEST_ASSERT_EQUAL_UINT64(23, IODiagnosticLatency__percentile(&chain->resume_to_reply, 50));
        },
        none_test_fail()
    );
}

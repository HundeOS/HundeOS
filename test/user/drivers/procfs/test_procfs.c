#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "sys/mem.h"
#include "../../../../user/drivers/procfs/procfs.h"

static ProcessRegistry registry;
static FilesystemDriver driver;

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

void setUp(void)
{
    memset(&registry, 0, sizeof(registry));
    registry.version = PROCESS_REGISTRY_VERSION;
    ProcessRecord record = {
        .pid = 42,
        .cpu = 3,
        .sampled_at_usec = 1000000,
        .name = bstr("test-process"),
        .state = PROC_STATE_WAITING,
        .wait_reason = Some(ParkReason, PARK_WAIT_IO),
        .process_class = PROCESS_CLASS_NORMAL,
        .credentials = ProcessCredentials__new(2, 2),
        .cpu_usage = {.total_usec = 104900,
                      .long_basis_points = 1049,
                      .short_basis_points = 2500,
                      .scheduled_usec = 120000,
                      .scheduled_long_basis_points = 1200,
                      .scheduled_short_basis_points = 3000},
        .queue_wait_usage = {.parks = 5,
                             .activity_wakes = 1,
                             .timeout_wakes = 3,
                             .selections = 4,
                             .speculative_selections = 2,
                             .parked_usec = 40000,
                             .max_parked_usec = 12000,
                             .runnable_usec = 8000,
                             .max_runnable_usec = 3000},
        .memory = Some(ProcessMemoryUsage,
                       ((ProcessMemoryUsage){.private_bytes = 8192, .shared_bytes = 4096, .device_bytes = 0}))};
    ProcessRegistry__set(&registry, 3, 0, &record);
    ProcessRegistry__set_count(&registry, 3, 1);
    FilesystemOperations operations = Procfs__operations();
    driver = FilesystemDriver__new(&registry, &operations);
}

void tearDown(void)
{
    TEST_ASSERT_TRUE(FilesystemDriver__release(&driver, 10, 20));
}

static IORequest request_for(IORequestOperation operation, ConstBStringPtr path)
{
    IORequest request = IORequest__new(operation, 0);
    request.flags = IO_OPEN_READ;
    request.client_pid = 20;
    request.client_session = 10;
    request.count = BString__length(path);
    memcpy(request.data, BString__cstr(path), request.count);
    return request;
}

static BString read_file(ConstBStringPtr path)
{
    IORequest request = request_for(IO_REQUEST_OPEN, path);
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.count = IO_PROTOCOL_DATA_SIZE;
    BString contents = empty_bstr();
    while (true)
    {
        IOReply reply = FilesystemDriver__execute(&driver, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        BString chunk = BString__from_chars((char *)reply.data, reply.value);
        contents = BString__append(&contents, &chunk);
        if (reply.value < request.count)
        {
            return contents;
        }
    }
}

typedef struct ProcfsFileContents
{
    size_t length;
    uint8_t data[2048];
} ProcfsFileContents;

static ProcfsFileContents read_file_contents(ConstBStringPtr path)
{
    IORequest request = request_for(IO_REQUEST_OPEN, path);
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.count = IO_PROTOCOL_DATA_SIZE;
    ProcfsFileContents contents = {0};
    while (true)
    {
        IOReply reply = FilesystemDriver__execute(&driver, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_LESS_OR_EQUAL_UINT64(sizeof(contents.data), contents.length + reply.value);
        memcpy(contents.data + contents.length, reply.data, reply.value);
        contents.length += reply.value;
        if (reply.value < request.count)
        {
            return contents;
        }
    }
}

static void IODiagnosticsTest__record_block_sample(uint64_t stage_usec, bool succeeded)
{
    Optional(BlockDeviceTiming) timing =
        BlockDeviceTiming__new(0, stage_usec, stage_usec * 2U, stage_usec * 3U, stage_usec * 4U, stage_usec * 5U);
    IODiagnostics__record_block_submission(8, stage_usec * 5U, succeeded, &timing);
}

void test_procfs_lists_processes_and_read_only_files_with_real_ownership(void)
{
    IORequest request = request_for(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_STRING("42", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0555, reply.directory.entries[0].mode);
    TEST_ASSERT_EQUAL(2, reply.directory.entries[0].user_id);
    request = request_for(IO_REQUEST_LIST_DIR, &bstr("/42"));
    const char * names[] = {"status", "cpu", "memory", "queue-wait", "ipc"};
    for (uint64_t index = 0; index < 5; index++)
    {
        request.offset = index;
        reply = FilesystemDriver__execute(&driver, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_EQUAL_STRING(names[index], reply.directory.entries[0].name);
        TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0444, reply.directory.entries[0].mode);
    }
    request.offset = 5;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).directory.count);
}

void test_procfs_reports_ipc_counters_and_unavailable_mapping(void)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("diagnostics=unavailable\n"), read_file(&bstr("/42/ipc")));
    IODiagnostics diagnostics = {0};
    IODiagnostics__attach(&diagnostics);
    IODiagnostics__record(IO_DIAGNOSTIC_UART_BYTES, 7);
    IODiagnostics__record(IO_DIAGNOSTIC_UART_IRQS, 2);
    IODiagnostics__record(IO_DIAGNOSTIC_UART_READS, 100);
    IODiagnostics__record(IO_DIAGNOSTIC_UART_EMPTY, 99);
    IODiagnostics__record(IO_DIAGNOSTIC_BACKEND_READS, 101);
    IODiagnostics__record(IO_DIAGNOSTIC_BACKEND_EMPTY, 98);
    IODiagnostics__record(IO_DIAGNOSTIC_WARDEN_RELAYS, 202);
    IODiagnostics__record(IO_DIAGNOSTIC_WAIT_ENTRIES, 300);
    IODiagnostics__record(IO_DIAGNOSTIC_UNCHANGED_WAKES, 250);
    IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_HITS, 77);
    IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_MISSES, 11);
    IODiagnostics__record(IO_DIAGNOSTIC_BLOCK_CACHE_EVICTIONS, 3);
    IODiagnostics__record(IO_DIAGNOSTIC_INODE_CACHE_HITS, 55);
    IODiagnostics__record(IO_DIAGNOSTIC_INODE_CACHE_MISSES, 9);
    IODiagnostics__record(IO_DIAGNOSTIC_INODE_CACHE_EVICTIONS, 2);
    IODiagnosticsTest__record_block_sample(1, false);
    IODiagnosticsTest__record_block_sample(4, true);
    IODiagnosticsTest__record_block_sample(16, true);
    IODiagnosticsTest__record_block_sample(64, true);
    ProcessRecord record = {
        .pid = 43,
        .sampled_at_usec = 1234,
        .user_wait_usec = 456,
        .io_diagnostics = Some(IODiagnosticsSnapshot, IODiagnostics__snapshot(&diagnostics)),
    };
    ProcessRegistry__set(&registry, 3, 1, &record);
    ProcessRegistry__set_count(&registry, 3, 2);
    static const char expected[] = "uart_bytes=7\nuart_irqs=2\nuart_reads=100\nuart_empty=99\n"
                                   "backend_reads=101\nbackend_empty=98\nwarden_relays=202\n"
                                   "block_submissions=4\nblock_blocks=32\nblock_failures=1\n"
                                   "block_cache_hits=77\nblock_cache_misses=11\nblock_cache_evictions=3\n"
                                   "inode_cache_hits=55\ninode_cache_misses=9\ninode_cache_evictions=2\n"
                                   "block_device_usec_p50=23\nblock_device_usec_p70=95\n"
                                   "block_device_usec_p90=383\nblock_device_usec_p99=383\n"
                                   "block_segmented_submissions=4\n"
                                   "block_device_irq_usec_p50=4\nblock_device_irq_usec_p70=19\n"
                                   "block_device_irq_usec_p90=79\nblock_device_irq_usec_p99=79\n"
                                   "block_manager_wake_usec_p50=4\nblock_manager_wake_usec_p70=19\n"
                                   "block_manager_wake_usec_p90=79\nblock_manager_wake_usec_p99=79\n"
                                   "block_irq_relay_usec_p50=4\nblock_irq_relay_usec_p70=19\n"
                                   "block_irq_relay_usec_p90=79\nblock_irq_relay_usec_p99=79\n"
                                   "block_fiber_wake_usec_p50=4\nblock_fiber_wake_usec_p70=19\n"
                                   "block_fiber_wake_usec_p90=79\nblock_fiber_wake_usec_p99=79\n"
                                   "block_completion_usec_p50=4\nblock_completion_usec_p70=19\n"
                                   "block_completion_usec_p90=79\nblock_completion_usec_p99=79\n"
                                   "wait_entries=300\nunchanged_wakes=250\nuser_wait_usec=456\n"
                                   "sampled_at_usec=1234\n";
    ProcfsFileContents contents = read_file_contents(&bstr("/43/ipc"));
    TEST_ASSERT_EQUAL_UINT64(sizeof(expected) - 1U, contents.length);
    TEST_ASSERT_EQUAL_MEMORY(expected, contents.data, contents.length);
}

void test_procfs_cpu_matches_ps_and_memory_categories_are_separate(void)
{
    BString status = read_file(&bstr("/42/status"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&status), "state=PROC_STATE_WAITING"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&status), "wait=PARK_WAIT_IO"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&status), "cpu=3"));
    BString cpu = read_file(&bstr("/42/cpu"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&cpu), "total_usec=104900"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&cpu), "CPU% (1s)=10.49"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&cpu), "CPU% (100ms)=25.00"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&cpu), "scheduled_usec=120000"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&cpu), "SCHED CPU% (1s)=12.00"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&cpu), "SCHED CPU% (100ms)=30.00"));
    BString memory = read_file(&bstr("/42/memory"));
    TEST_ASSERT_NOT_NULL(strstr(BString__cstr(&memory), "private_bytes=8192\nshared_bytes=4096\ndevice_bytes=0"));
}

void test_procfs_exposes_queue_wait_diagnostics(void)
{
    TEST_ASSERT_EQUAL_BSTRING(
        bstr("parks=5\nactivity_wakes=1\ntimeout_wakes=3\nselections=4\nspeculative_selections=2\n"
             "parked_usec=40000\n"
             "max_parked_usec=12000\nrunnable_usec=8000\nmax_runnable_usec=3000\nsampled_at_usec=1000000\n"),
        read_file(&bstr("/42/queue-wait")));
}

void test_procfs_removal_blocks_new_opens_but_existing_open_keeps_snapshot(void)
{
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/42/status"));
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    ProcessRegistry__set_count(&registry, 3, 0);
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.count = 6;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_MEMORY("pid=42", reply.data, 6);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, FilesystemDriver__execute(&driver, &request).status);
}

void test_open_file_owns_its_path_after_the_request_is_reused(void)
{
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/42/status"));
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);

    memset(request.data, 'x', sizeof(request.data));
    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.count = 6;
    IOReply reply = FilesystemDriver__execute(&driver, &request);

    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_MEMORY("pid=42", reply.data, 6);
}

void test_procfs_rejects_bad_paths_and_mutations(void)
{
    const char * paths[] = {"/042/status", "/42/../status", "/42/status/", "/42//cpu",
                            "/43/status",  "/42/handles",   "/42/"};
    for (unsigned index = 0; index < sizeof(paths) / sizeof(paths[0]); index++)
    {
        BString path = BString__new(paths[index]);
        IORequest request = request_for(IO_REQUEST_OPEN, &path);
        TEST_ASSERT_EQUAL(IO_INVALID_PATH, FilesystemDriver__execute(&driver, &request).status);
    }
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/42/status"));
    request.flags = IO_OPEN_WRITE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.flags = IO_OPEN_EXECUTE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_REMOVE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_MKDIR;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
}

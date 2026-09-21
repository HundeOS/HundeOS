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
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    return BString__from_chars((char *)reply.data, reply.value);
}

void test_procfs_lists_processes_and_read_only_files_with_real_ownership(void)
{
    IORequest request = request_for(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_STRING("42", reply.entry.name);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0555, reply.entry.mode);
    TEST_ASSERT_EQUAL(2, reply.entry.user_id);
    request = request_for(IO_REQUEST_LIST_DIR, &bstr("/42"));
    const char * names[] = {"status", "cpu", "memory", "queue-wait", "ipc"};
    for (uint64_t index = 0; index < 5; index++)
    {
        request.offset = index;
        reply = FilesystemDriver__execute(&driver, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_EQUAL_STRING(names[index], reply.entry.name);
        TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0444, reply.entry.mode);
    }
    request.offset = 5;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
}

void test_procfs_reports_ipc_counters_and_unavailable_mapping(void)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("diagnostics=unavailable\n"), read_file(&bstr("/42/ipc")));
    ProcessRecord record = {.pid = 43,
                            .sampled_at_usec = 1234,
                            .user_wait_usec = 456,
                            .io_diagnostics = Some(IODiagnostics, ((IODiagnostics){.uart_bytes = 7,
                                                                                   .uart_reads = 100,
                                                                                   .uart_empty = 99,
                                                                                   .backend_reads = 101,
                                                                                   .backend_empty = 98,
                                                                                   .warden_relays = 202,
                                                                                   .wait_entries = 300,
                                                                                   .unchanged_wakes = 250,
                                                                                   .uart_irqs = 2}))};
    ProcessRegistry__set(&registry, 3, 1, &record);
    ProcessRegistry__set_count(&registry, 3, 2);
    TEST_ASSERT_EQUAL_BSTRING(bstr("uart_bytes=7\nuart_irqs=2\nuart_reads=100\nuart_empty=99\n"
                                   "backend_reads=101\nbackend_empty=98\nwarden_relays=202\n"
                                   "wait_entries=300\nunchanged_wakes=250\nuser_wait_usec=456\n"
                                   "sampled_at_usec=1234\n"),
                              read_file(&bstr("/43/ipc")));
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
        bstr("parks=5\nactivity_wakes=1\ntimeout_wakes=3\nselections=4\nparked_usec=40000\n"
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

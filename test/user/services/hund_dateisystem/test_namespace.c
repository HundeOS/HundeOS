#include "unity.h"
#define ServiceDirectory__shared NamespaceTest__directory
#include "../../../../user/services/hund-dateisystem/namespace.c"
#undef ServiceDirectory__shared
#include "sys/print.h"
#include "sys/spawn.h"
#include "sys/io.h"
#include "sys/syslog.h"
#define main filesystem_main_under_test
#include "../../../../user/services/hund-dateisystem/main.c"
#undef main

static FilesystemNamespace namespace;
static ServiceDirectory directory;

static const char * boot_fstab;
static bool spawn_fails;
static bool root_ready;
static unsigned root_spawns;
static unsigned tmp_spawns;
static bool tmp_ready;
static bool tmp_spawn_fails;
static unsigned ready_waits;
static unsigned ready_publications;
static unsigned listens;
static unsigned dev_spawns;
static bool dev_ready;
static bool dev_spawn_fails;
static unsigned proc_spawns;
static bool proc_ready;
static bool proc_spawn_fails;

static unsigned sbin_spawns;
static bool sbin_ready;
static bool sbin_spawn_fails;

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config/fstab"), *path);
    return Success(IOHandle, 7);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    TEST_ASSERT_EQUAL(7, handle);
    return Success(IOStatus, IO_OK);
}

Result(DynString) IO__read_dyn_string(IOHandle handle, StringPos capacity)
{
    DynString text;
    match_result(DynString__init(&text, capacity),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(DynString__append_cstr(&text, boot_fstab),
        success_do_nothing(),
        error_test_fail()
    );
    return Success(DynString, text);
}

Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0,
                              OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_ASSERT_EQUAL(P_NOWAIT, mode);
    if (BString__equals(path, &bstr("/sbin/drivers/embedfs")))
    {
        TEST_ASSERT_EQUAL(4, ready_waits);
        sbin_spawns++;
        return sbin_spawn_fails ? Error(SpawnResult, bstr("spawn failed"), SPAWN_FAILED) : Success(SpawnResult, 74);
    }
    if (BString__equals(path, &bstr("/sbin/drivers/procfs")))
    {
        TEST_ASSERT_EQUAL(3, ready_waits);
        proc_spawns++;
        return proc_spawn_fails ? Error(SpawnResult, bstr("spawn failed"), SPAWN_FAILED) : Success(SpawnResult, 73);
    }
    if (BString__equals(path, &bstr("/sbin/drivers/devfs")))
    {
        TEST_ASSERT_EQUAL(2, ready_waits);
        dev_spawns++;
        return dev_spawn_fails ? Error(SpawnResult, bstr("spawn failed"), SPAWN_FAILED) : Success(SpawnResult, 72);
    }
    if (BString__equals(path, &bstr("/sbin/drivers/memfs")))
    {
        BString mountpoint = unwrap_optional(*arg0, empty_bstr());
        TEST_ASSERT_EQUAL_BSTRING(bstr("/tmp"), mountpoint);
        match_optional(*arg1,
            some(value)
            {
                TEST_FAIL_MESSAGE("memfs has no device argument");
            },
            none_do_nothing()
        );
        match_optional(*arg2,
            some(value)
            {
                TEST_FAIL_MESSAGE("unexpected argument");
            },
            none_do_nothing()
        );
        TEST_ASSERT_EQUAL(1, ready_waits);
        tmp_spawns++;
        return tmp_spawn_fails ? Error(SpawnResult, bstr("spawn failed"), SPAWN_FAILED) : Success(SpawnResult, 71);
    }
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/services/humpen-wachhund"), *path);
    BString device = unwrap_optional(*arg0, empty_bstr());
    BString format = unwrap_optional(*arg1, empty_bstr());
    BString mountpoint = unwrap_optional(*arg2, empty_bstr());
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/virtio1"), device);
    TEST_ASSERT_EQUAL_BSTRING(bstr("ext2"), format);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), mountpoint);
    TEST_ASSERT_EQUAL(1, listens);
    root_spawns++;
    return spawn_fails ? Error(SpawnResult, bstr("spawn failed"), SPAWN_FAILED) : Success(SpawnResult, 70);
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_authorize_mount_client(uint64_t client_pid)
{
    return client_pid == 60 || client_pid == 61 ? SERVICE_OK : SERVICE_PERMISSION_DENIED;
}

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    if (BString__equals(name, &bstr("/sbin")))
    {
        TEST_ASSERT_EQUAL(1, sbin_spawns);
        ready_waits++;
        return sbin_ready ? SERVICE_OK : SERVICE_SCHEDULER_ERROR;
    }
    if (BString__equals(name, &bstr("/proc")))
    {
        TEST_ASSERT_EQUAL(1, proc_spawns);
        ready_waits++;
        return proc_ready ? SERVICE_OK : SERVICE_SCHEDULER_ERROR;
    }
    if (BString__equals(name, &bstr("/dev")))
    {
        TEST_ASSERT_EQUAL(1, dev_spawns);
        ready_waits++;
        return dev_ready ? SERVICE_OK : SERVICE_SCHEDULER_ERROR;
    }
    if (BString__equals(name, &bstr("/tmp")))
    {
        TEST_ASSERT_EQUAL(1, tmp_spawns);
        ready_waits++;
        return tmp_ready ? SERVICE_OK : SERVICE_SCHEDULER_ERROR;
    }
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), *name);
    TEST_ASSERT_EQUAL(1, root_spawns);
    ready_waits++;
    return root_ready ? SERVICE_OK : SERVICE_SCHEDULER_ERROR;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_FILESYSTEM_SERVICE), *name);
    TEST_ASSERT_EQUAL(0, ready_waits);
    ready_publications++;
    // Stop at the readiness boundary without entering the service loop.
    return SERVICE_SCHEDULER_ERROR;
}

IOStatus IOService__listen_into(IOServicePtr self, ConstBStringPtr name, IOServiceContext context,
                                IOServiceExecute execute, IOServiceDisconnect disconnect)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_FILESYSTEM_SERVICE), *name);
    listens++;
    *self = (IOService){0};
    return IO_OK;
}

bool IOService__poll(IOServicePtr self)
{
    TEST_FAIL_MESSAGE("Unexpected service loop");
    return false;
}

void Channel__prepare_wait(void)
{
}

void Channel__wait_event(void)
{
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return 0;
}

ServiceDirectoryPtr NamespaceTest__directory(void)
{
    return &directory;
}

int IO__printf(ConstBStringPtr format, ...)
{
    TEST_FAIL_MESSAGE("Unexpected collection error");
    return 0;
}

void setUp(void)
{
    dev_spawns = 0;
    dev_ready = true;
    dev_spawn_fails = false;
    proc_spawns = 0;
    proc_ready = true;
    proc_spawn_fails = false;
    sbin_spawns = 0;
    sbin_ready = true;
    sbin_spawn_fails = false;
    boot_fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio4 fat32 /mnt/fat\n";
    spawn_fails = false;
    root_ready = true;
    root_spawns = ready_waits = ready_publications = listens = 0;
    tmp_spawns = 0;
    tmp_ready = true;
    tmp_spawn_fails = false;
    namespace = FilesystemNamespace__new();
    directory = (ServiceDirectory){
        .magic = HUND_SERVICE_DIRECTORY_MAGIC, .version = HUND_SERVICE_DIRECTORY_VERSION, .entry_count = 4};
    directory.entries[0] =
        (ServiceDirectoryEntry){.present = 1, .generation = 1, .service_pid = 50, .name = bstr(IO_SYSTEM_SERVICE)};
    directory.entries[1] =
        (ServiceDirectoryEntry){.present = 1, .generation = 1, .service_pid = 51, .name = bstr("/mnt/fat")};
    directory.entries[2] =
        (ServiceDirectoryEntry){.present = 1, .generation = 1, .service_pid = 52, .name = bstr("/mnt/ext")};
    directory.entries[3] = (ServiceDirectoryEntry){.present = 1, .generation = 1, .service_pid = 53, .name = bstr("/")};
    match_result(
        MountConfig__parse(&bstr("device /dev/virtio4 fat32 /mnt/fat # boot")),
        success(config)
        {
            match_result(FilesystemNamespace__add(&namespace, config),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    match_result(
        MountConfig__parse(&bstr("device /dev/virtio6 ext2 /mnt/ext")),
        success(config)
        {
            match_result(FilesystemNamespace__add(&namespace, config),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void tearDown(void)
{
}

static IORequest path_request(IORequestOperation operation, ConstBStringPtr path)
{
    IORequest request = IORequest__new(operation, 0);
    request.flags = IO_OPEN_READ;
    request.client_pid = 100;
    request.client_session = 200;
    request.count = BString__copy_to(path, (char *)request.data, sizeof(request.data));
    return request;
}

static IOReply execute(IORequestPtr request)
{
    return unwrap_optional(FilesystemNamespace__execute(&namespace, 30, 50, request), IOReply__new(IO_PANIC, 0));
}

static void add_root(void)
{
    MountConfig root = {.device_path = bstr("/dev/virtio1"), .filesystem_type = bstr("ext2"), .mountpoint = bstr("/")};
    match_result(FilesystemNamespace__add(&namespace, root),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_hosted_ext2_is_a_supported_configured_mount(void)
{
    MountConfig hosted = MountConfig__new(&bstr("/dev/virtio7"), &bstr("ext2.wuff"), &bstr("/mnt/hosted-ext2"));
    match_result(FilesystemNamespace__add(&namespace, hosted),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_root_routes_absolute_paths_and_nested_mounts_win_regardless_of_order(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello.txt"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/hello.txt"), IORoute__path(&reply.route));
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fat"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/file"), IORoute__path(&reply.route));
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fatter/file"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), IORoute__service(&reply.route));
    namespace = FilesystemNamespace__new();
    add_root();
    MountConfig nested = {
        .device_path = bstr("/dev/virtio4"), .filesystem_type = bstr("fat32"), .mountpoint = bstr("/mnt/fat")};
    match_result(FilesystemNamespace__add(&namespace, nested),
        success_do_nothing(),
        error_test_fail()
    );
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fat"), IORoute__service(&reply.route));
}

void test_namespace_routes_only_complete_valid_path_components(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/HELLO.TXT"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_TRUE(reply.route.final);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fat"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/HELLO.TXT"), IORoute__path(&reply.route));
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fatter/file"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/../ext/file"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
}

void test_namespace_lists_immediate_mount_children_before_routing_the_backend(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(1, reply.directory.count);
    TEST_ASSERT_EQUAL_STRING("mnt", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, reply.directory.entries[0].mode);

    request.offset = 1;
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_UINT64(0, reply.value);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), IORoute__path(&reply.route));
}

void test_namespace_lists_nested_mount_children_without_duplicates(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/mnt"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(2, reply.directory.count);
    TEST_ASSERT_EQUAL_STRING("fat", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL_STRING("ext", reply.directory.entries[1].name);

    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/mnt/"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(2, reply.directory.count);
    TEST_ASSERT_EQUAL_STRING("fat", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL_STRING("ext", reply.directory.entries[1].name);

    MountConfig nested = {
        .device_path = bstr("/dev/virtio5"), .filesystem_type = bstr("fat32"), .mountpoint = bstr("/mnt/fat/nested")};
    match_result(FilesystemNamespace__add(&namespace, nested),
        success_do_nothing(),
        error_test_fail()
    );
    request.offset = 0;
    reply = execute(&request);
    TEST_ASSERT_EQUAL_UINT64(2, reply.directory.count);
}

void test_namespace_pages_mount_children_and_adjusts_the_backend_offset(void)
{
    add_root();
    MountConfig sbin = {.device_path = bstr("-"), .filesystem_type = bstr("embedfs"), .mountpoint = bstr("/sbin")};
    MountConfig tmp = {.device_path = bstr("-"), .filesystem_type = bstr("memfs"), .mountpoint = bstr("/tmp")};
    MountConfig dev = {.device_path = bstr("-"), .filesystem_type = bstr("devfs"), .mountpoint = bstr("/dev")};
    match_result(FilesystemNamespace__add(&namespace, sbin),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FilesystemNamespace__add(&namespace, tmp),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(FilesystemNamespace__add(&namespace, dev),
        success_do_nothing(),
        error_test_fail()
    );

    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(IO_DIRECTORY_BATCH_CAPACITY, reply.directory.count);

    request.offset = IO_DIRECTORY_BATCH_CAPACITY;
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_UINT64(1, reply.directory.count);

    request.offset = 6;
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_UINT64(2, reply.value);
}

void test_namespace_routes_file_information_without_synthetic_results(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_FILE_INFO, &bstr("/boot"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot"), IORoute__path(&reply.route));
    request = path_request(IO_REQUEST_FILE_INFO, &bstr("/boot/sysinfo"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/sysinfo"), IORoute__path(&reply.route));
}

void test_duplicate_root_is_rejected(void)
{
    add_root();
    MountConfig root = {.device_path = bstr("/dev/virtio1"), .filesystem_type = bstr("ext2"), .mountpoint = bstr("/")};
    match_result(
        FilesystemNamespace__add(&namespace, root),
        success(code)
        {
            TEST_FAIL_MESSAGE("Duplicate root accepted");
        },
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IO_INVALID_MOUNT_POINT, code);
        }
    );
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello.txt"));
    TEST_ASSERT_EQUAL(IO_REDIRECT, execute(&request).status);
}

void test_namespace_rejects_handle_operations_and_missing_identity(void)
{
    IORequest request = IORequest__new(IO_REQUEST_READ, IO_SESSION_HANDLE_BASE);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(&request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    IOReply reply =
        unwrap_optional(FilesystemNamespace__execute(&namespace, 30, 0, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, reply.status);
}

void test_namespace_starts_without_reading_configuration_or_spawning_drivers(void)
{
    TEST_ASSERT_EQUAL(1, filesystem_main_under_test());
    TEST_ASSERT_EQUAL(1, ready_publications);
    TEST_ASSERT_EQUAL(0, root_spawns);
    TEST_ASSERT_EQUAL(0, ready_waits);
}

void test_mount_request_rejects_untrusted_client(void)
{
    namespace = FilesystemNamespace__new();
    IORequest request = path_request(IO_REQUEST_MOUNT, &bstr("device - embedfs /sbin"));
    IOReply reply =
        unwrap_optional(FilesystemNamespace__execute(&namespace, 45, 12345, &request), IOReply__new(IO_OK, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, reply.status);
    TEST_ASSERT_EQUAL(0, FilesystemMounts__len(&namespace.mounts));
}

void test_mama_can_add_boot_mount_before_root_and_duplicate_mount_is_rejected(void)
{
    namespace = FilesystemNamespace__new();
    IORequest request = path_request(IO_REQUEST_MOUNT, &bstr("device - sysinfofs /boot"));
    IOReply reply =
        unwrap_optional(FilesystemNamespace__execute(&namespace, 45, 60, &request), IOReply__new(IO_INVALID_PATH, 0));
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    reply = unwrap_optional(FilesystemNamespace__execute(&namespace, 45, 60, &request), IOReply__new(IO_OK, 0));
    TEST_ASSERT_EQUAL(IO_INVALID_MOUNT_POINT, reply.status);
    add_root();
    match_optional(
        FilesystemNamespace__route(&namespace, &bstr("/boot/sysinfo")),
        some(mount)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("/boot"), mount->config.mountpoint);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(2, FilesystemMounts__len(&namespace.mounts));
    request = path_request(IO_REQUEST_MOUNT, &bstr("device - memfs /tmp"));
    reply =
        unwrap_optional(FilesystemNamespace__execute(&namespace, 45, 61, &request), IOReply__new(IO_INVALID_PATH, 0));
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
}

void test_namespace_owns_mount_text_after_the_request_is_reused(void)
{
    namespace = FilesystemNamespace__new();
    IORequest request = path_request(IO_REQUEST_MOUNT, &bstr("device /dev/virtio4 fat32 /mnt/fat"));
    IOReply mounted =
        unwrap_optional(FilesystemNamespace__execute(&namespace, 45, 60, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_OK, mounted.status);

    __builtin_memset(request.data, 'x', sizeof(request.data));
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    IOReply routed = execute(&request);

    TEST_ASSERT_EQUAL(IO_REDIRECT, routed.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fat"), IORoute__service(&routed.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/file"), IORoute__path(&routed.route));
}

void test_link_routes_both_paths_relative_to_the_same_mount(void)
{
    IORequest request = path_request(IO_REQUEST_LINK, &bstr("/mnt/fat/source"));
    IORequest__set_target_path(&request, &bstr("/mnt/fat/target"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_REDIRECT, reply.status);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fat"), IORoute__service(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/source"), IORoute__path(&reply.route));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/target"), IORoute__target_path(&reply.route));
}

void test_link_rejects_cross_mount_and_invalid_targets(void)
{
    IORequest request = path_request(IO_REQUEST_LINK, &bstr("/mnt/fat/source"));
    IORequest__set_target_path(&request, &bstr("/mnt/ext/target"));
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(&request).status);
    IORequest__set_target_path(&request, &bstr("/missing/target"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
    IORequest__set_target_path(&request, &bstr("/mnt/fat/../ext/target"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
    request.target_path_length = IO_PROTOCOL_DATA_SIZE;
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
}

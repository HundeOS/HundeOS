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

typedef struct FakeFilesystem
{
    IORequest request;
    bool queued;
    bool blocked;
    bool broken;
    IOHandle next_handle;
    unsigned opens;
    unsigned releases;
    IOStatus list_status;
} FakeFilesystem;

static FakeFilesystem backends[3];
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

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config/fstab"), *path);
    return 7;
}

IOStatus IO__close(IOHandle handle)
{
    TEST_ASSERT_EQUAL(7, handle);
    return IO_OK;
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos capacity)
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
    return Some(DynString, text);
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

Result(Channel) Channel__open(ServiceDirectoryPtr registry, ConstBStringPtr service)
{
    return Success(Channel, ((Channel){.token = BString__equals(service, &bstr("/"))          ? 3
                                                : BString__equals(service, &bstr("/mnt/fat")) ? 1
                                                                                              : 2}));
}

Result(ChannelCode) Channel__request(ChannelPtr self, ConstChannelDataPtr data, size_t size)
{
    FakeFilesystem * backend = &backends[self->token - 1];
    TEST_ASSERT_FALSE(backend->queued);
    if (backend->broken)
    {
        return Error(ChannelCode, bstr("backend exited"), CHANNEL_UNAVAILABLE);
    }
    __builtin_memcpy(&backend->request, data, size);
    backend->queued = true;
    return Success(ChannelCode, CHANNEL_OK);
}

Result(OptionalChannelReplySize) Channel__poll_reply(ChannelPtr self, ChannelDataPtr data, size_t capacity)
{
    FakeFilesystem * backend = &backends[self->token - 1];
    TEST_ASSERT_TRUE(backend->queued);
    if (backend->blocked)
    {
        return Success(OptionalChannelReplySize, None(ChannelReplySize));
    }
    IOReply reply = IOReply__new(IO_OK, 0);
    switch (backend->request.operation)
    {
    case IO_REQUEST_OPEN:
        reply.value = backend->next_handle++;
        backend->opens++;
        break;
    case IO_REQUEST_RELEASE_CLIENT:
        backend->releases++;
        break;
    case IO_REQUEST_READ:
        reply.value = backend->request.count;
        __builtin_memset(reply.data, 'x', reply.value);
        break;
    case IO_REQUEST_LIST_DIR:
        reply.status = backend->list_status;
        if (reply.status == IO_OK && backend->request.offset == 0)
        {
            reply.directory.entries[0] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("hello.txt"));
            reply.directory.count = 1;
        }
        break;
    case IO_REQUEST_FILE_INFO:
        reply.file_info = (IOFileInfo){.mode = IO_MODE_TYPE_FILE | 0644,
                                       .user_id = PROCESS_USER_HUND,
                                       .group_id = PROCESS_GROUP_HUND,
                                       .size = 123};
        break;
    default:
        break;
    }
    backend->queued = false;
    __builtin_memcpy(data, &reply, sizeof(reply));
    return Success(OptionalChannelReplySize, Some(ChannelReplySize, sizeof(reply)));
}

ChannelCode Channel__close(ChannelPtr self)
{
    backends[self->token - 1].queued = false;
    return CHANNEL_OK;
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
    backends[0] = (FakeFilesystem){.next_handle = IO_SESSION_HANDLE_BASE};
    backends[1] = (FakeFilesystem){.next_handle = IO_SESSION_HANDLE_BASE};
    backends[2] = (FakeFilesystem){.next_handle = IO_SESSION_HANDLE_BASE};
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

static void add_root(void);

void test_namespace_routes_only_complete_mount_path_components(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/HELLO.TXT"));
    IOReply opened = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, opened.value);
    BString relative = IORequest__path(&backends[0].request);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/HELLO.TXT"), relative);
    TEST_ASSERT_EQUAL(100, backends[0].request.client_pid);
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fatter/file"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/../ext/file"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
}

void test_namespace_lists_mount_parents_without_duplicate_names(void)
{
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_STRING("mnt", reply.directory.entries[0].name);
    request.offset = 6;
    TEST_ASSERT_EQUAL(0, execute(&request).directory.count);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/mnt"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL_STRING("fat", reply.directory.entries[0].name);
    request.offset = 1;
    TEST_ASSERT_EQUAL_STRING("ext", execute(&request).directory.entries[0].name);
}

void test_namespace_reports_synthetic_and_forwarded_file_information(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_FILE_INFO, &bstr("/boot"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, reply.file_info.mode);

    request = path_request(IO_REQUEST_FILE_INFO, &bstr("/boot/sysinfo"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0444, reply.file_info.mode);

    request = path_request(IO_REQUEST_FILE_INFO, &bstr("/hello.txt"));
    reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0644, reply.file_info.mode);
    TEST_ASSERT_EQUAL(123, reply.file_info.size);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/hello.txt"), IORequest__path(&backends[2].request));
}

void test_namespace_authenticates_mediator_and_scopes_handles(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    IOReply denied =
        unwrap_optional(FilesystemNamespace__execute(&namespace, 30, 99, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, denied.status);
    IOReply opened = execute(&request);
    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.count = 8;
    request.client_session++;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(&request).status);
    request.client_session--;
    TEST_ASSERT_EQUAL(8, execute(&request).value);
    request.operation = IO_REQUEST_CLOSE;
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, execute(&request).status);
}

void test_blocked_mount_does_not_hold_namespace_channel(void)
{
    IORequest first = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    backends[0].blocked = true;
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(&first).status);
    IORequest second = path_request(IO_REQUEST_OPEN, &bstr("/mnt/ext/file"));
    second.client_session++;
    second.client_pid++;
    TEST_ASSERT_EQUAL(IO_OK, execute(&second).status);
    backends[0].blocked = false;
    TEST_ASSERT_EQUAL(IO_OK, execute(&first).status);
    TEST_ASSERT_EQUAL(1, backends[0].opens);
}

void test_namespace_cleans_open_completed_after_client_exit(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    backends[0].blocked = true;
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(&request).status);
    request.operation = IO_REQUEST_RELEASE_CLIENT;
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, execute(&request).status);
    backends[0].blocked = false;
    IOReply reply = execute(&request);
    for (unsigned i = 0; i < 10 && reply.status == IO_WAIT_FOR_DATA; i++)
    {
        reply = execute(&request);
    }
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(1, backends[0].opens);
    TEST_ASSERT_EQUAL(1, backends[0].releases);
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    TEST_ASSERT_EQUAL(1, backends[0].releases);
}

void test_failed_mount_never_becomes_unmanaged(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    backends[0].broken = true;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(&request).status);
    backends[0].broken = false;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(&request).status);
    TEST_ASSERT_EQUAL(0, backends[0].opens);
}

static void add_root(void)
{
    MountConfig root = {.device_path = bstr("/dev/virtio1"), .filesystem_type = bstr("ext2"), .mountpoint = bstr("/")};
    match_result(FilesystemNamespace__add(&namespace, root),
        success_do_nothing(),
        error_test_fail()
    );
}

void test_root_routes_absolute_paths_and_nested_mounts_win_regardless_of_order(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello.txt"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    BString relative = IORequest__path(&backends[2].request);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/hello.txt"), relative);
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    TEST_ASSERT_EQUAL(1, backends[0].opens);
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fatter/file"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    TEST_ASSERT_EQUAL(2, backends[2].opens);
    namespace = FilesystemNamespace__new();
    add_root();
    MountConfig nested = {
        .device_path = bstr("/dev/virtio4"), .filesystem_type = bstr("fat32"), .mountpoint = bstr("/mnt/fat")};
    match_result(FilesystemNamespace__add(&namespace, nested),
        success_do_nothing(),
        error_test_fail()
    );
    request = path_request(IO_REQUEST_OPEN, &bstr("/mnt/fat/file"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    TEST_ASSERT_EQUAL(2, backends[0].opens);
}

void test_root_listing_includes_bootstrap_overlays_then_disk_entries(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(3, reply.directory.count);
    TEST_ASSERT_EQUAL_STRING("mnt", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL_STRING("sbin", reply.directory.entries[1].name);
    TEST_ASSERT_EQUAL_STRING("boot", reply.directory.entries[2].name);
    request.offset = 3;
    reply = execute(&request);
    TEST_ASSERT_EQUAL(1, reply.directory.count);
    TEST_ASSERT_EQUAL_STRING("hello.txt", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL(0, backends[2].request.offset);
    request.offset = 4;
    TEST_ASSERT_EQUAL(0, execute(&request).directory.count);
}

void test_root_preserves_bootstrap_paths_and_synthetic_parents(void)
{
    add_root();
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/sbin/commands/ls"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/boot/config/fstab"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/boot/sysinfo"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    backends[2].list_status = IO_INVALID_PATH;
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/boot"));
    TEST_ASSERT_EQUAL_STRING("config", execute(&request).directory.entries[0].name);
    request.offset = 1;
    IOReply reply = execute(&request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_STRING("sysinfo", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0444, reply.directory.entries[0].mode);
    request.offset = 2;
    reply = execute(&request);
    TEST_ASSERT_EQUAL(0, reply.directory.count);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/missing"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, execute(&request).status);
}

void test_sbin_mount_routes_to_embedfs_and_does_not_fall_back_on_failure(void)
{
    add_root();
    directory.entry_count = 5;
    directory.entries[4] =
        (ServiceDirectoryEntry){.present = 1, .generation = 1, .service_pid = 54, .name = bstr("/sbin")};
    MountConfig mount = MountConfig__new(&empty_bstr(), &bstr("embedfs"), &bstr("/sbin"));
    match_result(FilesystemNamespace__add(&namespace, mount),
        success_do_nothing(),
        error_test_fail()
    );
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/sbin/hello"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    BString relative = IORequest__path(&backends[1].request);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/hello"), relative);
    backends[1].broken = true;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(&request).status);
}

void test_duplicate_root_is_rejected_and_failed_root_has_no_kernel_fallback(void)
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
    backends[2].broken = true;
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello.txt"));
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(&request).status);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    request.offset = 6;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(&request).status);
}

void test_tmp_mount_is_managed_even_when_its_driver_is_unavailable(void)
{
    add_root();
    MountConfig tmp = MountConfig__new(&empty_bstr(), &bstr("memfs"), &bstr("/tmp"));
    match_result(FilesystemNamespace__add(&namespace, tmp),
        success_do_nothing(),
        error_test_fail()
    );
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/tmp/file"));
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, execute(&request).status);
    TEST_ASSERT_EQUAL(0, backends[2].opens);
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

void test_link_routes_both_paths_relative_to_the_same_mount(void)
{
    IORequest request = path_request(IO_REQUEST_LINK, &bstr("/mnt/fat/source"));
    IORequest__set_target_path(&request, &bstr("/mnt/fat/target"));
    TEST_ASSERT_EQUAL(IO_OK, execute(&request).status);
    TEST_ASSERT_EQUAL(IO_REQUEST_LINK, backends[0].request.operation);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/source"), IORequest__path(&backends[0].request));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/target"), IORequest__target_path(&backends[0].request));
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
    TEST_ASSERT_EQUAL(0, backends[0].request.operation);
    TEST_ASSERT_EQUAL(0, backends[1].request.operation);
}

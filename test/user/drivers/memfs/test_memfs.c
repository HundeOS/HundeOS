#include <stdlib.h>
#include "unity.h"
#define ServiceDirectory__shared MemfsTest__directory
#include "../../../../user/drivers/memfs/memfs.c"
#undef ServiceDirectory__shared

static Memfs filesystem;
static ServiceDirectory directory;
static bool allocation_fails;
static unsigned credential_lookups;

Result(ProcessCredentials) process_credentials(uint64_t pid)
{
    credential_lookups++;
    if (pid == 20)
    {
        return Success(ProcessCredentials, ProcessCredentials__new(PROCESS_USER_USER, PROCESS_GROUP_USER));
    }
    if (pid == 21)
    {
        return Success(ProcessCredentials, ProcessCredentials__new(PROCESS_USER_HUND, PROCESS_GROUP_HUND));
    }
    if (pid == 22)
    {
        return Success(ProcessCredentials, ProcessCredentials__new(42, PROCESS_GROUP_USER));
    }
    if (pid == 23)
    {
        return Success(ProcessCredentials, ProcessCredentials__new(PROCESS_USER_ROOT, PROCESS_GROUP_ROOT));
    }
    return Error(ProcessCredentials, bstr("Process exited"), PROCESS_CREDENTIALS_NOT_FOUND);
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    if (allocation_fails)
    {
        return Error(Anything, bstr("exhausted"), 1);
    }
    return Success(Anything, (Anything)malloc(size));
}

void setUp(void)
{
    filesystem = Memfs__new();
    allocation_fails = false;
    credential_lookups = 0;
    __builtin_memset(&directory, 0, sizeof(directory));
    directory.magic = HUND_SERVICE_DIRECTORY_MAGIC;
    directory.version = HUND_SERVICE_DIRECTORY_VERSION;
    directory.entry_count = 1;
    directory.entries[0].present = 1;
    directory.entries[0].generation = 1;
    directory.entries[0].service_pid = 70;
    directory.entries[0].name = bstr(IO_FILESYSTEM_SERVICE);
}

void tearDown(void)
{
    Memfs__dispose(&filesystem);
}

ServiceDirectoryPtr MemfsTest__directory(void)
{
    return &directory;
}

static IORequest request_for(IORequestOperation operation, IOHandle handle)
{
    IORequest request = IORequest__new(operation, handle);
    request.client_pid = 20;
    request.client_session = 30;
    return request;
}

static IORequest path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags)
{
    IORequest request = request_for(operation, 0);
    request.count = BString__copy_to(path, (char *)request.data, IO_PROTOCOL_DATA_SIZE);
    request.flags = flags;
    return request;
}

static IOHandle open_file(ConstBStringPtr path, Flags flags)
{
    IORequest request = path_request(IO_REQUEST_OPEN, path, flags);
    IOReply reply = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    return reply.value;
}

static IOReply operation(IORequestOperation kind, IOHandle handle)
{
    IORequest request = request_for(kind, handle);
    request.count = kind == IO_REQUEST_READ ? IO_PROTOCOL_DATA_SIZE : 0;
    return Memfs__execute(&filesystem, &request);
}

void test_memfs_grows_beyond_disk_snapshot_limit_and_persists_after_close(void)
{
    IOHandle writer = open_file(&bstr("/large"), IO_OPEN_WRITE);
    IORequest request = request_for(IO_REQUEST_WRITE, writer);
    request.count = IO_PROTOCOL_DATA_SIZE;
    for (unsigned i = 0; i < 80; i++)
    {
        __builtin_memset(request.data, i, sizeof(request.data));
        IOReply reply = Memfs__execute(&filesystem, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_EQUAL(IO_PROTOCOL_DATA_SIZE, reply.value);
    }
    TEST_ASSERT_EQUAL(IO_OK, operation(IO_REQUEST_CLOSE, writer).status);
    IOHandle reader = open_file(&bstr("/large"), IO_OPEN_READ);
    for (unsigned i = 0; i < 80; i++)
    {
        IOReply reply = operation(IO_REQUEST_READ, reader);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_EQUAL(IO_PROTOCOL_DATA_SIZE, reply.value);
        for (unsigned j = 0; j < IO_PROTOCOL_DATA_SIZE; j++)
        {
            TEST_ASSERT_EQUAL(i, reply.data[j]);
        }
    }
    TEST_ASSERT_EQUAL(0, operation(IO_REQUEST_READ, reader).value);
}

void test_memfs_empty_create_truncate_and_independent_handles(void)
{
    IOHandle writer = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IOHandle reader = open_file(&bstr("/file"), IO_OPEN_READ);
    IORequest request = request_for(IO_REQUEST_WRITE, writer);
    request.count = 3;
    __builtin_memcpy(request.data, "abc", 3);
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    IOReply reply = operation(IO_REQUEST_READ, reader);
    TEST_ASSERT_EQUAL(3, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("abc", reply.data, 3);
    IOHandle second = open_file(&bstr("/file"), IO_OPEN_READ);
    TEST_ASSERT_EQUAL(3, operation(IO_REQUEST_READ, second).value);
    open_file(&bstr("/file"), IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(0, operation(IO_REQUEST_READ, reader).value);
    IOHandle empty = open_file(&bstr("/file"), IO_OPEN_READ);
    TEST_ASSERT_EQUAL(0, operation(IO_REQUEST_READ, empty).value);
}

void test_memfs_failed_growth_preserves_contents_and_offset(void)
{
    IOHandle writer = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest request = request_for(IO_REQUEST_WRITE, writer);
    request.count = 1;
    request.data[0] = 'a';
    allocation_fails = true;
    TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, Memfs__execute(&filesystem, &request).status);
    allocation_fails = false;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    IOHandle reader = open_file(&bstr("/file"), IO_OPEN_READ);
    IOReply reply = operation(IO_REQUEST_READ, reader);
    TEST_ASSERT_EQUAL(1, reply.value);
    TEST_ASSERT_EQUAL('a', reply.data[0]);
}

void test_memfs_rejects_foreign_stale_and_over_capacity_handles(void)
{
    IOHandle writer = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest request = request_for(IO_REQUEST_CLOSE, writer);
    request.client_pid++;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Memfs__execute(&filesystem, &request).status);
    request.client_pid--;
    request.client_session++;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Memfs__execute(&filesystem, &request).status);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, operation(IO_REQUEST_READ, writer).status);
    TEST_ASSERT_EQUAL(IO_OK, operation(IO_REQUEST_CLOSE, writer).status);
    for (unsigned i = 0; i < 32; i++)
    {
        open_file(&bstr("/file"), IO_OPEN_READ);
    }
    request = path_request(IO_REQUEST_OPEN, &bstr("/file"), IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, Memfs__execute(&filesystem, &request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, operation(IO_REQUEST_CLOSE, writer).status);
    TEST_ASSERT_EQUAL(IO_OK, operation(IO_REQUEST_RELEASE_CLIENT, 0).status);
    TEST_ASSERT_EQUAL(0, MemfsHandles__len(&filesystem.handles));
    open_file(&bstr("/file"), IO_OPEN_READ);
}

void test_memfs_failed_reallocation_preserves_existing_file(void)
{
    IOHandle writer = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest request = request_for(IO_REQUEST_WRITE, writer);
    request.count = IO_PROTOCOL_DATA_SIZE;
    __builtin_memset(request.data, 'a', sizeof(request.data));
    for (unsigned i = 0; i < 16; i++)
    {
        TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    }
    allocation_fails = true;
    TEST_ASSERT_EQUAL(IO_REGISTER_FAILED, Memfs__execute(&filesystem, &request).status);
    IOHandle reader = open_file(&bstr("/file"), IO_OPEN_READ);
    for (unsigned i = 0; i < 16; i++)
    {
        IOReply reply = operation(IO_REQUEST_READ, reader);
        TEST_ASSERT_EQUAL(IO_PROTOCOL_DATA_SIZE, reply.value);
        TEST_ASSERT_EQUAL_MEMORY(request.data, reply.data, IO_PROTOCOL_DATA_SIZE);
    }
    TEST_ASSERT_EQUAL(0, operation(IO_REQUEST_READ, reader).value);
    allocation_fails = false;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    TEST_ASSERT_EQUAL(IO_PROTOCOL_DATA_SIZE, operation(IO_REQUEST_READ, reader).value);
}

void test_memfs_truncation_zero_fills_gap_from_an_existing_writer_offset(void)
{
    IOHandle writer = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest request = request_for(IO_REQUEST_WRITE, writer);
    request.count = 3;
    __builtin_memcpy(request.data, "abc", 3);
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    open_file(&bstr("/file"), IO_OPEN_WRITE);
    request.count = 1;
    request.data[0] = 'x';
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    IOReply reply = operation(IO_REQUEST_READ, open_file(&bstr("/file"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(4, reply.value);
    TEST_ASSERT_EQUAL(0, reply.data[0]);
    TEST_ASSERT_EQUAL(0, reply.data[1]);
    TEST_ASSERT_EQUAL(0, reply.data[2]);
    TEST_ASSERT_EQUAL('x', reply.data[3]);
}

void test_memfs_directories_recursive_remove_and_recreated_paths_do_not_revive_handles(void)
{
    IORequest request = path_request(IO_REQUEST_MKDIR, &bstr("/docs"), 0);
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    IOHandle old = open_file(&bstr("/docs/file"), IO_OPEN_WRITE);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/docs"), 0);
    IOReply reply = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(1, reply.directory.count);
    TEST_ASSERT_EQUAL_STRING("file", reply.directory.entries[0].name);
    request.offset = 1;
    TEST_ASSERT_EQUAL(0, Memfs__execute(&filesystem, &request).directory.count);
    request = path_request(IO_REQUEST_REMOVE, &bstr("/docs"), 0);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, Memfs__execute(&filesystem, &request).status);
    request.flags = IO_REMOVE_RECURSIVE;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_MKDIR, &bstr("/docs"), 0);
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    open_file(&bstr("/docs/file"), IO_OPEN_WRITE);
    request = request_for(IO_REQUEST_WRITE, old);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Memfs__execute(&filesystem, &request).status);
}

void test_memfs_keeps_more_than_64_files_and_validates_paths_and_lengths(void)
{
    for (unsigned i = 0; i < 70; i++)
    {
        BString path = BString__sprintf(&bstr("/file%u"), i);
        IOHandle handle = open_file(&path, IO_OPEN_WRITE);
        TEST_ASSERT_EQUAL(IO_OK, operation(IO_REQUEST_CLOSE, handle).status);
    }
    TEST_ASSERT_EQUAL(70, MemfsNodes__len(&filesystem.nodes));
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/../bad"), IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/missing/file"), IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, Memfs__execute(&filesystem, &request).status);
    request.count = IO_PROTOCOL_DATA_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, Memfs__execute(&filesystem, &request).status);
}

void test_memfs_authenticates_namespace_and_disconnect_releases_handles_not_files(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/file"), IO_OPEN_WRITE);
    IOReply denied = unwrap_optional(Memfs__serve(&filesystem, 100, 71, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, denied.status);
    IOReply opened = unwrap_optional(Memfs__serve(&filesystem, 100, 70, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    denied = unwrap_optional(Memfs__serve(&filesystem, 101, 70, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, denied.status);
    Memfs__disconnect(&filesystem, 101, 70);
    TEST_ASSERT_EQUAL(1, MemfsHandles__len(&filesystem.handles));
    Memfs__disconnect(&filesystem, 100, 70);
    TEST_ASSERT_EQUAL(0, MemfsHandles__len(&filesystem.handles));
    TEST_ASSERT_EQUAL(1, MemfsNodes__len(&filesystem.nodes));
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, operation(IO_REQUEST_CLOSE, opened.value).status);
    open_file(&bstr("/file"), IO_OPEN_READ);
}

void test_memfs_records_creator_identity_and_non_executable_file_mode(void)
{
    open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"), 0);
    IOReply reply = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(PROCESS_USER_USER, reply.directory.entries[0].user_id);
    TEST_ASSERT_EQUAL(PROCESS_GROUP_USER, reply.directory.entries[0].group_id);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0644, reply.directory.entries[0].mode);
    request = path_request(IO_REQUEST_OPEN, &bstr("/file"), IO_OPEN_EXECUTE);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_MKDIR, &bstr("/docs"), 0);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"), 0);
    request.offset = 1;
    reply = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(PROCESS_USER_HUND, reply.directory.entries[0].user_id);
    TEST_ASSERT_EQUAL(PROCESS_GROUP_HUND, reply.directory.entries[0].group_id);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, reply.directory.entries[0].mode);
}

void test_memfs_reports_file_information_without_listing_parent(void)
{
    IOHandle handle = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest write = IORequest__new(IO_REQUEST_WRITE, handle);
    write.client_pid = 20;
    write.client_session = 30;
    write.count = 4;
    __builtin_memcpy(write.data, "data", 4);
    TEST_ASSERT_EQUAL(4, Memfs__execute(&filesystem, &write).value);

    IORequest request = path_request(IO_REQUEST_FILE_INFO, &bstr("/file"), 0);
    IOReply reply = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0644, reply.file_info.mode);
    TEST_ASSERT_EQUAL(4, reply.file_info.size);
}

void test_memfs_other_user_can_read_but_cannot_truncate_or_remove_file(void)
{
    IOHandle writer = open_file(&bstr("/file"), IO_OPEN_WRITE);
    IORequest request = request_for(IO_REQUEST_WRITE, writer);
    request.count = 1;
    request.data[0] = 'x';
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/file"), IO_OPEN_WRITE);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request.flags = IO_OPEN_READ;
    IOReply opened = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_READ, opened.value);
    request.client_pid = 21;
    request.count = 1;
    IOReply reply = Memfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(1, reply.value);
    TEST_ASSERT_EQUAL('x', reply.data[0]);
    request = path_request(IO_REQUEST_REMOVE, &bstr("/file"), 0);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request.client_pid = 20;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
}

void test_memfs_directory_permissions_and_sticky_tmp_protect_other_users(void)
{
    IORequest request = path_request(IO_REQUEST_MKDIR, &bstr("/docs"), 0);
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    open_file(&bstr("/docs/file"), IO_OPEN_WRITE);
    request = path_request(IO_REQUEST_OPEN, &bstr("/docs/foreign"), IO_OPEN_WRITE);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_REMOVE, &bstr("/docs/file"), 0);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_REMOVE, &bstr("/docs"), IO_REMOVE_RECURSIVE);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    match_optional(Memfs__find(&filesystem, &bstr("/docs")),
        some(node)
        {
            node->mode = 0700;
        },
        none_test_fail()
    );
    request = path_request(IO_REQUEST_OPEN, &bstr("/docs/file"), IO_OPEN_READ);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/docs"), 0);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
}

void test_memfs_group_bits_and_root_identity_follow_permission_rules(void)
{
    open_file(&bstr("/file"), IO_OPEN_WRITE);
    match_optional(Memfs__find(&filesystem, &bstr("/file")),
        some(node)
        {
            node->mode = 0640;
        },
        none_test_fail()
    );
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/file"), IO_OPEN_READ);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request.client_pid = 22;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    request.flags = IO_OPEN_WRITE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
    request.client_pid = 23;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    request.flags = IO_OPEN_EXECUTE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Memfs__execute(&filesystem, &request).status);
}

void test_memfs_missing_pid_fails_closed_but_cleanup_does_not_resolve_credentials(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/file"), IO_OPEN_WRITE);
    request.client_pid = 999;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_PID, Memfs__execute(&filesystem, &request).status);
    TEST_ASSERT_EQUAL(0, MemfsNodes__len(&filesystem.nodes));
    unsigned previous = credential_lookups;
    request = request_for(IO_REQUEST_RELEASE_CLIENT, 0);
    request.client_pid = 999;
    TEST_ASSERT_EQUAL(IO_OK, Memfs__execute(&filesystem, &request).status);
    TEST_ASSERT_EQUAL(previous, credential_lookups);
    request = path_request(IO_REQUEST_OPEN, &bstr("/forged"), IO_OPEN_WRITE);
    IOReply denied = unwrap_optional(Memfs__serve(&filesystem, 100, 999, &request), IOReply__new(IO_PANIC, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, denied.status);
    TEST_ASSERT_EQUAL(previous, credential_lookups);
}

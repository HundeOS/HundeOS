#include "unity.h"

#include <string.h>

#define ServiceDirectory__shared EmbedfsTest__directory
#include "../../../../user/drivers/embedfs/embedfs.c"
#undef ServiceDirectory__shared
#include <stddef.h>

static struct
{
    EmbeddedFsHeader header;
    EmbeddedFsEntry entries[3];
    char names[128];
    uint8_t bytes[20000];
} blob;

static Embedfs filesystem;
static ServiceDirectory directory;
static bool exited;

ServiceDirectoryPtr EmbedfsTest__directory(void)
{
    return &directory;
}

Result(ProcessCredentials) process_credentials(uint64_t pid)
{
    if (exited)
    {
        return Error(ProcessCredentials, bstr("Exited"), PROCESS_CREDENTIALS_NOT_FOUND);
    }
    return Success(ProcessCredentials, ProcessCredentials__new(pid == 21 ? PROCESS_USER_HUND : PROCESS_USER_USER,
                                                               pid == 21 ? PROCESS_GROUP_HUND : PROCESS_GROUP_USER));
}

void setUp(void)
{
    memset(&blob, 0, sizeof(blob));
    blob.header = (EmbeddedFsHeader){.magic = EMBEDDED_FS_MAGIC,
                                     .version = EMBEDDED_FS_VERSION,
                                     .entry_count = 3,
                                     .index_offset = offsetof(typeof(blob), entries),
                                     .names_offset = offsetof(typeof(blob), names),
                                     .data_offset = offsetof(typeof(blob), bytes),
                                     .blob_size = sizeof(blob)};
    const char * names[] = {"hello", "services/first", "services/second"};
    size_t offset = 0;
    for (unsigned index = 0; index < 3; index++)
    {
        size_t len = strlen(names[index]);
        memcpy(blob.names + offset, names[index], len);
        blob.entries[index] = (EmbeddedFsEntry){.data_offset = offsetof(typeof(blob), bytes),
                                                .data_len = sizeof(blob.bytes),
                                                .name_offset = offsetof(typeof(blob), names) + offset,
                                                .name_len = len};
        offset += len;
    }
    for (size_t index = 0; index < sizeof(blob.bytes); index++)
    {
        blob.bytes[index] = index % 251;
    }
    EmbeddedFs archive = EmbeddedFs__new((Address)&blob, (Address)&blob + sizeof(blob));
    Embedfs__init(&filesystem, &archive);
    memset(&directory, 0, sizeof(directory));
    directory.magic = HUND_SERVICE_DIRECTORY_MAGIC;
    directory.version = HUND_SERVICE_DIRECTORY_VERSION;
    directory.entry_count = 1;
    directory.entries[0].present = 1;
    directory.entries[0].generation = 1;
    directory.entries[0].service_pid = 70;
    directory.entries[0].name = bstr(IO_FILESYSTEM_SERVICE);
    exited = false;
}

void tearDown(void)
{
}

void test_init_uses_existing_storage(void)
{
    TEST_ASSERT_EQUAL(0, EmbedfsHandles__len(&filesystem.handles));
    TEST_ASSERT_EQUAL(0, BStringArray__len(&filesystem.children));
    TEST_ASSERT_EQUAL(IO_SESSION_HANDLE_BASE, filesystem.next_handle);
}

static IORequest request_for(IORequestOperation operation, IOHandle handle)
{
    IORequest request = IORequest__new(operation, handle);
    request.client_pid = 20;
    request.client_session = 30;
    return request;
}

static IORequest path_request(IORequestOperation operation, ConstBStringPtr path)
{
    IORequest request = request_for(operation, 0);
    request.count = BString__copy_to(path, (char *)request.data, sizeof(request.data));
    request.flags = IO_OPEN_READ;
    return request;
}

static IOHandle open_hello(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello"));
    IOReply reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_GREATER_OR_EQUAL(IO_SESSION_HANDLE_BASE, reply.value);
    return reply.value;
}

void test_large_archive_file_reads_to_eof_without_snapshot_limit(void)
{
    IORequest request = request_for(IO_REQUEST_READ, open_hello());
    request.count = IO_PROTOCOL_DATA_SIZE;
    size_t total = 0;
    while (total < sizeof(blob.bytes))
    {
        IOReply reply = Embedfs__execute(&filesystem, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_GREATER_THAN(0, reply.value);
        TEST_ASSERT_EQUAL_MEMORY(blob.bytes + total, reply.data, reply.value);
        total += reply.value;
    }
    TEST_ASSERT_EQUAL(sizeof(blob.bytes), total);
    TEST_ASSERT_EQUAL(0, Embedfs__execute(&filesystem, &request).value);
}

void test_handles_have_independent_offsets_and_authenticated_owners(void)
{
    IOHandle first = open_hello();
    IOHandle second = open_hello();
    IORequest request = request_for(IO_REQUEST_READ, first);
    request.count = 10;
    TEST_ASSERT_EQUAL(10, Embedfs__execute(&filesystem, &request).value);
    request.handle = second;
    IOReply reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL_MEMORY(blob.bytes, reply.data, 10);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Embedfs__execute(&filesystem, &request).status);
    request.client_pid = 20;
    request.client_session = 31;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Embedfs__execute(&filesystem, &request).status);
}

void test_close_and_exit_release_handles_even_after_credentials_disappear(void)
{
    IOHandle first = open_hello();
    IOHandle second = open_hello();
    exited = true;
    IORequest request = request_for(IO_REQUEST_CLOSE, first);
    TEST_ASSERT_EQUAL(IO_OK, Embedfs__execute(&filesystem, &request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Embedfs__execute(&filesystem, &request).status);
    request = request_for(IO_REQUEST_RELEASE_CLIENT, 0);
    TEST_ASSERT_EQUAL(IO_OK, Embedfs__execute(&filesystem, &request).status);
    request = request_for(IO_REQUEST_READ, second);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, Embedfs__execute(&filesystem, &request).status);
}

void test_service_files_and_directories_require_hund_credentials(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/services/first"));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Embedfs__execute(&filesystem, &request).status);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_OK, Embedfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_LIST_DIR, &bstr("/services"));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Embedfs__execute(&filesystem, &request).status);
    request.client_pid = 21;
    IOReply reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_STRING("first", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0550, reply.directory.entries[0].mode);
}

void test_root_listing_deduplicates_directories_and_reports_metadata(void)
{
    IORequest request = path_request(IO_REQUEST_LIST_DIR, &bstr("/"));
    IOReply reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL_STRING("hello", reply.directory.entries[0].name);
    request.offset = 1;
    reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL_STRING("services", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0550, reply.directory.entries[0].mode);
    request.offset = 2;
    TEST_ASSERT_EQUAL(0, Embedfs__execute(&filesystem, &request).value);
}

void test_embedfs_reports_file_and_directory_information(void)
{
    IORequest request = path_request(IO_REQUEST_FILE_INFO, &bstr("/hello"));
    IOReply reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0555, reply.file_info.mode);
    TEST_ASSERT_EQUAL(sizeof(blob.bytes), reply.file_info.size);

    request = path_request(IO_REQUEST_FILE_INFO, &bstr("/services"));
    request.client_pid = 21;
    reply = Embedfs__execute(&filesystem, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0550, reply.file_info.mode);
    TEST_ASSERT_EQUAL(PROCESS_USER_HUND, reply.file_info.user_id);
}

void test_mutation_unknown_processes_and_parent_traversal_are_rejected(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello"));
    request.flags = IO_OPEN_WRITE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, Embedfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/../hello"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, Embedfs__execute(&filesystem, &request).status);
    request = path_request(IO_REQUEST_OPEN, &bstr("/hello"));
    exited = true;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_PID, Embedfs__execute(&filesystem, &request).status);
}

void test_direct_clients_are_isolated_and_disconnect_releases_their_resources(void)
{
    IORequest request = path_request(IO_REQUEST_OPEN, &bstr("/hello"));
    match_optional(
        Embedfs__serve(&filesystem, 30, 20, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
        },
        none_test_fail()
    );
    request = path_request(IO_REQUEST_OPEN, &bstr("/services/first"));
    request.client_pid = 21;
    request.client_session = 31;
    match_optional(
        Embedfs__serve(&filesystem, 31, 21, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(2, EmbedfsHandles__len(&filesystem.handles));
    TEST_ASSERT_TRUE(Embedfs__disconnect(&filesystem, 31, 21));
    TEST_ASSERT_EQUAL(1, EmbedfsHandles__len(&filesystem.handles));
    TEST_ASSERT_TRUE(Embedfs__disconnect(&filesystem, 30, 20));
    TEST_ASSERT_EQUAL(0, EmbedfsHandles__len(&filesystem.handles));

    match_optional(
        Embedfs__serve(&filesystem, 30, 0, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, reply.status);
        },
        none_test_fail()
    );
}

void test_archive_entry_bounds_reject_overflow_and_names_need_no_terminator(void)
{
    open_hello();
    blob.entries[0].data_len = UINT64_MAX;
    match_optional(EmbeddedFs__at(&filesystem.archive, 0),
        some_test_fail(),
        none_do_nothing()
    );
    blob.entries[0].name_offset = UINT32_MAX;
    match_optional(EmbeddedFs__at(&filesystem.archive, 0),
        some_test_fail(),
        none_do_nothing()
    );
    blob.header.index_offset++;
    TEST_ASSERT_FALSE(EmbeddedFs__is_valid(&filesystem.archive));
}

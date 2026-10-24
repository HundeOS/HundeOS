#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "sys/mem.h"
#include "fs/devfs.h"
#include "hunde/filesystem.h"

static DeviceRegistry registry;
static FilesystemDriver driver;

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

void setUp(void)
{
    memset(&registry, 0, sizeof(registry));
    registry.version = DEVICE_REGISTRY_VERSION;
    DeviceRecord block = DeviceRecord__new(DEVICE_BLOCK, 12, &bstr("/dev/virtio1"), &bstr("hd0"), 512, 16384);
    DeviceRecord stream = DeviceRecord__new(DEVICE_KERNEL_STREAM, 13, &bstr("/dev/test-stream"), &empty_bstr(), 0, 0);
    match_result(DeviceRegistry__set(&registry, 0, &block),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(DeviceRegistry__set(&registry, 1, &stream),
        success_do_nothing(),
        error_test_fail()
    );
    FilesystemOperations operations = Devfs__operations();
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

void test_devfs_lists_devices_and_aliases_read_only(void)
{
    const char * expected[] = {"null", "virtio1", "hd0", "test-stream"};
    FileMode expected_modes[] = {IO_MODE_TYPE_FILE | 0666, IO_MODE_TYPE_FILE | 0444, IO_MODE_TYPE_FILE | 0444,
                                 IO_MODE_TYPE_FILE | 0444};
    IORequest request = request_for(IO_REQUEST_LIST_DIR, &bstr("/"));
    for (uint64_t index = 0; index < 4; index++)
    {
        request.offset = index;
        IOReply reply = FilesystemDriver__execute(&driver, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_EQUAL_STRING(expected[index], reply.directory.entries[0].name);
        TEST_ASSERT_EQUAL(expected_modes[index], reply.directory.entries[0].mode);
    }
    request.offset = 4;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).directory.count);
}

void test_devfs_root_reports_directory_metadata_before_listing(void)
{
    IORequest request = request_for(IO_REQUEST_FILE_INFO, &bstr("/"));
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, reply.file_info.mode);
    TEST_ASSERT_EQUAL_UINT64(0, reply.file_info.size);
    request.operation = IO_REQUEST_LIST_DIR;
    reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_GREATER_THAN_UINT64(0, reply.directory.count);
}

static HundeIORequestV6 HundeIORequestV6__new(IORequestOperation operation)
{
    return (HundeIORequestV6){.operation = operation, .session = 10, .pid = 20};
}

void test_hosted_devfs_preserves_listing_paths_and_pagination(void)
{
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_LIST_DIR);
    request.write_data = (const uint8_t *)"/";
    request.count = request.capacity = 1;
    HundeIOCompletion reply = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, reply.kind);
    TEST_ASSERT_EQUAL_UINT64(3, reply.directory_count);
    TEST_ASSERT_EQUAL_BSTRING(
        bstr("null"), BString__from_chars((const char *)reply.directory[0].name, reply.directory[0].name_length));
    TEST_ASSERT_EQUAL_BSTRING(
        bstr("virtio1"), BString__from_chars((const char *)reply.directory[1].name, reply.directory[1].name_length));
    request.offset = 3;
    reply = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, reply.kind);
    TEST_ASSERT_EQUAL_UINT64(1, reply.directory_count);
    TEST_ASSERT_EQUAL_BSTRING(bstr("test-stream"), BString__from_chars((const char *)reply.directory[0].name,
                                                                       reply.directory[0].name_length));
    request.offset = 4;
    reply = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, reply.kind);
    TEST_ASSERT_EQUAL_UINT64(0, reply.directory_count);
}

static HundeIOCompletion Devfs__open_native(ConstBStringPtr path, Flags flags)
{
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_OPEN);
    request.flags = flags;
    request.write_data = (const uint8_t *)BString__cstr(path);
    request.count = request.capacity = BString__length(path);
    return hunde_rust_devfs_execute(&driver, &request);
}

void test_hosted_devfs_root_and_layout_use_the_existing_driver_storage(void)
{
    RustNativeLayout layout = hunde_rust_devfs_layout();
    TEST_ASSERT_EQUAL_UINT64(sizeof(driver), layout.size);
    TEST_ASSERT_EQUAL_UINT64(_Alignof(FilesystemDriver), layout.alignment);
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_FILE_INFO);
    request.write_data = (const uint8_t *)"/";
    request.count = request.capacity = 1;
    HundeIOCompletion result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_UINT32(IO_MODE_TYPE_DIR | 0755, result.file_info.mode);
    TEST_ASSERT_EQUAL_UINT64(0, result.directory_count);
}

void test_hosted_devfs_uses_bounded_caller_buffers_beyond_inline_channel_capacity(void)
{
    HundeIOCompletion opened = Devfs__open_native(&bstr("/null"), IO_OPEN_READ | IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, opened.kind);
    uint8_t buffer[IO_PROTOCOL_DATA_SIZE + 300];
    memset(buffer, 42, sizeof(buffer));
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_WRITE);
    request.handle = opened.value;
    request.write_data = buffer;
    request.count = request.capacity = sizeof(buffer);
    HundeIOCompletion result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_UINT64(sizeof(buffer), result.value);
    request.operation = IO_REQUEST_READ;
    request.read_data = buffer;
    result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_UINT64(0, result.value);
    request.capacity--;
    result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, result.error.code);
}

void test_hosted_devfs_preserves_inherited_offsets_and_target_authentication_after_disconnect(void)
{
    HundeIOCompletion opened = Devfs__open_native(&bstr("/hd0"), IO_OPEN_READ);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, opened.kind);
    uint8_t buffer[5];
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_READ);
    request.handle = opened.value;
    request.count = request.capacity = sizeof(buffer);
    request.read_data = buffer;
    HundeIOCompletion result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_MEMORY("path=", buffer, sizeof(buffer));
    request.pid = 21;
    result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, result.error.code);
    request = HundeIORequestV6__new(IO_REQUEST_INHERIT_PREPARE);
    request.handle = opened.value;
    HundeIOCompletion prepared = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, prepared.kind);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, prepared.inheritance_flags);
    request.operation = IO_REQUEST_INHERIT_COMMIT;
    request.inheritance_token = prepared.value;
    request.inheritance_pid = 30;
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_devfs_execute(&driver, &request).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_devfs_disconnect(&driver, 10, 20).kind);
    request = HundeIORequestV6__new(IO_REQUEST_INHERIT_CLAIM);
    request.session = 11;
    request.pid = 31;
    request.inheritance_token = prepared.value;
    request.flags = IO_OPEN_READ;
    result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, result.error.code);
    request.pid = 30;
    result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_INT64(opened.value, result.value);
    request.operation = IO_REQUEST_READ;
    request.handle = result.value;
    request.flags = 0;
    request.read_data = buffer;
    request.count = request.capacity = sizeof(buffer);
    result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_MEMORY("/dev/", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_devfs_disconnect(&driver, 11, 30).kind);
    TEST_ASSERT_EQUAL_UINT64(0, FilesystemFiles__len(&driver.files));
}

void test_native_filesystem_codec_bounds_links_and_directory_completions(void)
{
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_LINK);
    request.write_data = (const uint8_t *)"/from/to";
    request.offset = 5;
    request.count = request.capacity = 8;
    match_result(
        IORequest__new_rust(&request),
        success(decoded)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("/from"), IORequest__path(&decoded));
            TEST_ASSERT_EQUAL_BSTRING(bstr("/to"), IORequest__target_path(&decoded));
        },
        error_test_fail()
    );
    request.offset = 0;
    match_result(
        IORequest__new_rust(&request),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(IO_INVALID_PATH, code);
        }
    );
    IOReply reply = IOReply__new(IO_OK, 0);
    reply.directory.count = IO_DIRECTORY_BATCH_CAPACITY + 1;
    HundeIOCompletion result = HundeIOCompletion__new_reply(IO_REQUEST_LIST_DIR, reply);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, result.error.code);
    reply.directory.count = 1;
    reply.directory.entries[0].name_len = IO_DIRENT_NAME_MAX;
    result = HundeIOCompletion__new_reply(IO_REQUEST_LIST_DIR, reply);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, result.error.code);
    result = HundeIOCompletion__new_reply(IO_REQUEST_READ, IOReply__new(IO_WAIT_FOR_DATA, 0));
    TEST_ASSERT_EQUAL(HUNDE_IO_PENDING, result.kind);
}

void test_hosted_devfs_rejects_terminal_controls_without_reading_an_input_payload(void)
{
    HundeIOCompletion opened = Devfs__open_native(&bstr("/null"), IO_OPEN_READ);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, opened.kind);
    IOTerminalAttributes attributes = IOTerminalAttributes__default();
    HundeIORequestV6 request = HundeIORequestV6__new(IO_REQUEST_GET_TERMINAL_ATTRIBUTES);
    request.handle = opened.value;
    request.read_data = (uint8_t *)&attributes;
    request.count = request.capacity = sizeof(attributes);
    HundeIOCompletion result = hunde_rust_devfs_execute(&driver, &request);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, result.error.code);
    IOTerminalAttributes expected = IOTerminalAttributes__default();
    TEST_ASSERT_EQUAL_MEMORY(&expected, &attributes, sizeof(attributes));
}

void test_devfs_null_reads_eof_and_discards_writes(void)
{
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/null"));
    request.flags = IO_OPEN_READ | IO_OPEN_WRITE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);

    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.flags = 0;
    request.count = 8;
    IOReply read = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, read.status);
    TEST_ASSERT_EQUAL(0, read.value);

    request.operation = IO_REQUEST_WRITE;
    request.count = 4;
    memcpy(request.data, "data", request.count);
    IOReply written = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, written.status);
    TEST_ASSERT_EQUAL(4, written.value);
}

void test_devfs_reads_alias_metadata_with_independent_offsets_and_owner_checks(void)
{
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/hd0"));
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    IOReply second = FilesystemDriver__execute(&driver, &request);
    request.operation = IO_REQUEST_READ;
    request.handle = opened.value;
    request.count = 5;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(5, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("path=", reply.data, 5);
    request.handle = second.value;
    TEST_ASSERT_EQUAL_MEMORY("path=", FilesystemDriver__execute(&driver, &request).data, 5);
    request.client_pid = 21;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, FilesystemDriver__execute(&driver, &request).status);
}

void test_devfs_rejects_mutation_and_execute_but_preserves_kernel_stream_opens(void)
{
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/virtio1"));
    request.flags = IO_OPEN_WRITE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request.operation = IO_REQUEST_WRITE;
    request.handle = opened.value;
    request.flags = 0;
    request.count = 1;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, &bstr("/virtio1"));
    request.flags = IO_OPEN_EXECUTE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_REMOVE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_MKDIR;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, &bstr("/test-stream"));
    TEST_ASSERT_EQUAL(IO_PATH_UNMANAGED, FilesystemDriver__execute(&driver, &request).status);
    request.flags = IO_OPEN_WRITE;
    opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request.operation = IO_REQUEST_WRITE;
    request.handle = opened.value;
    request.flags = 0;
    request.count = 1;
    TEST_ASSERT_EQUAL(IO_PATH_UNMANAGED, FilesystemDriver__execute(&driver, &request).status);
}

void test_devfs_observes_removal_and_rejects_invalid_registry(void)
{
    DeviceRecord empty = DeviceRecord__new(DEVICE_ABSENT, 0, &empty_bstr(), &empty_bstr(), 0, 0);
    match_result(DeviceRegistry__set(&registry, 0, &empty),
        success_do_nothing(),
        error_test_fail()
    );
    IORequest request = request_for(IO_REQUEST_OPEN, &bstr("/hd0"));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, FilesystemDriver__execute(&driver, &request).status);
    registry.version++;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, FilesystemDriver__execute(&driver, &request).status);
}

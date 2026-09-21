#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "sys/mem.h"
#include "../../../../user/drivers/devfs/devfs.h"

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
    const char * expected[] = {"virtio1", "hd0", "test-stream"};
    IORequest request = request_for(IO_REQUEST_LIST_DIR, &bstr("/"));
    for (uint64_t index = 0; index < 3; index++)
    {
        request.offset = index;
        IOReply reply = FilesystemDriver__execute(&driver, &request);
        TEST_ASSERT_EQUAL(IO_OK, reply.status);
        TEST_ASSERT_EQUAL_STRING(expected[index], reply.entry.name);
        TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0444, reply.entry.mode);
    }
    request.offset = 3;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
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
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.flags = IO_OPEN_EXECUTE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_REMOVE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_MKDIR;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, &bstr("/test-stream"));
    TEST_ASSERT_EQUAL(IO_PATH_UNMANAGED, FilesystemDriver__execute(&driver, &request).status);
    request.flags = IO_OPEN_WRITE;
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

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/service.h"

#define FAT32FS_ENTRYPOINT fat32fs_main_under_test
#include "../../../../user/drivers/fat32fs/main.c"

#define TEST_FAT32FS_SECTOR_COUNT 64U

static uint8_t disk[TEST_FAT32FS_SECTOR_COUNT][FAT32FS_BYTES_PER_SECTOR];
static BString expected_device_path;
static ServiceFilesystemRequest next_filesystem_request;
static bool has_next_filesystem_request;
static ServiceFilesystemCompletion last_filesystem_completion;

static void write_u16(uint8_t * data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t) (value & 0xff);
    data[offset + 1] = (uint8_t) ((value >> 8) & 0xff);
}

static void write_u32(uint8_t * data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t) (value & 0xff);
    data[offset + 1] = (uint8_t) ((value >> 8) & 0xff);
    data[offset + 2] = (uint8_t) ((value >> 16) & 0xff);
    data[offset + 3] = (uint8_t) ((value >> 24) & 0xff);
}

static void write_fat_entry(uint32_t cluster, uint32_t value)
{
    size_t offset = cluster * sizeof(uint32_t);
    write_u32(disk[32], offset, value);
    write_u32(disk[33], offset, value);
}

static void write_short_name(uint8_t * entry, const char * name)
{
    memset(entry, ' ', 11);
    size_t out = 0;
    size_t in = 0;
    while (name[in] != '\0' && name[in] != '.' && out < 8)
    {
        entry[out++] = (uint8_t) name[in++];
    }
    if (name[in] == '.')
    {
        in++;
        out = 8;
        while (name[in] != '\0' && out < 11)
        {
            entry[out++] = (uint8_t) name[in++];
        }
    }
}

static void write_dirent(uint8_t * sector, size_t offset, const char * name, uint8_t attr, uint32_t cluster, uint32_t size)
{
    write_short_name(&sector[offset], name);
    sector[offset + 11] = attr;
    write_u16(sector, offset + 20, (uint16_t) ((cluster >> 16) & 0xffff));
    write_u16(sector, offset + 26, (uint16_t) (cluster & 0xffff));
    write_u32(sector, offset + 28, size);
}

static void build_test_disk(void)
{
    memset(disk, 0, sizeof(disk));

    disk[0][0] = 0xeb;
    disk[0][1] = 0x3c;
    disk[0][2] = 0x90;
    write_u16(disk[0], 11, FAT32FS_BYTES_PER_SECTOR);
    disk[0][13] = FAT32FS_SECTORS_PER_CLUSTER;
    write_u16(disk[0], 14, 32);
    disk[0][16] = FAT32FS_FAT_COUNT;
    write_u16(disk[0], 17, 0);
    write_u16(disk[0], 22, 0);
    write_u32(disk[0], 32, TEST_FAT32FS_SECTOR_COUNT);
    write_u32(disk[0], 36, 1);
    write_u32(disk[0], 44, FAT32FS_ROOT_CLUSTER);
    disk[0][510] = 0x55;
    disk[0][511] = 0xaa;

    write_fat_entry(0, 0x0ffffff8);
    write_fat_entry(1, FAT32FS_END_OF_CHAIN);
    write_fat_entry(2, FAT32FS_END_OF_CHAIN);
    write_fat_entry(3, FAT32FS_END_OF_CHAIN);
    write_fat_entry(4, FAT32FS_END_OF_CHAIN);

    memcpy(disk[35], "hello fat32\n", 12);
    write_dirent(disk[34], 0, "HELLO.TXT", FAT32FS_ATTR_ARCHIVE, 3, 12);
    write_dirent(disk[34], 32, "DOCS", FAT32FS_ATTR_DIRECTORY, 4, 0);
}

void setUp(void)
{
    expected_device_path = bstr("/dev/test");
    has_next_filesystem_request = false;
    last_filesystem_completion = (ServiceFilesystemCompletion) { 0 };
    build_test_disk();
}

void tearDown(void)
{
}

Result(ServiceBlockReadResult) IO__block_read(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    TEST_ASSERT_TRUE(BString__equals(path, &expected_device_path));
    TEST_ASSERT_EQUAL_UINT64(1, block_count);
    TEST_ASSERT_EQUAL_UINT64(FAT32FS_BYTES_PER_SECTOR, buffer_size);
    TEST_ASSERT_LESS_THAN_UINT64(TEST_FAT32FS_SECTOR_COUNT, block);
    memcpy((void *) buffer, disk[block], FAT32FS_BYTES_PER_SECTOR);
    return Success(ServiceBlockReadResult, block_count);
}

ServiceBlockReadResult service_block_read_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    match_result(ServiceBlockReadResult, IO__block_read(path, block, block_count, buffer, buffer_size),
        success(result)
        {
            return result;
        },
        error(code, message)
        {
            return (ServiceBlockReadResult) code;
        }
    );

    return SERVICE_BLOCK_REQUEST_FAILED;
}

Result(ServiceBlockReadResult) IO__block_write(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    TEST_ASSERT_TRUE(BString__equals(path, &expected_device_path));
    TEST_ASSERT_EQUAL_UINT64(1, block_count);
    TEST_ASSERT_EQUAL_UINT64(FAT32FS_BYTES_PER_SECTOR, buffer_size);
    TEST_ASSERT_LESS_THAN_UINT64(TEST_FAT32FS_SECTOR_COUNT, block);
    memcpy(disk[block], (void *) buffer, FAT32FS_BYTES_PER_SECTOR);
    return Success(ServiceBlockReadResult, block_count);
}

ServiceBlockReadResult service_block_write_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    match_result(ServiceBlockReadResult, IO__block_write(path, block, block_count, buffer, buffer_size),
        success(result)
        {
            return result;
        },
        error(code, message)
        {
            return (ServiceBlockReadResult) code;
        }
    );

    return SERVICE_BLOCK_REQUEST_FAILED;
}

AsyncIO AsyncIO__new(Anything service_context, AsyncIORequestService request_service)
{
    return (AsyncIO) {
        .service_context = service_context,
        .request_service = request_service,
    };
}

Result(AsyncIOCode) AsyncIO__start(
    AsyncIOPtr self,
    AsyncIORegisterService register_service,
    Anything service_context,
    AsyncIORequestService request_service
)
{
    match_result(AsyncIOCode, register_service(service_context),
        success_do_nothing(),
        error(code, message)
        {
            return Error(AsyncIOCode, message, code);
        }
    );

    return Success(AsyncIOCode, ASYNC_IO_WAIT_COMPLETE);
}

Result(AsyncIOCode) AsyncIO__start_with_init(
    AsyncIOPtr self,
    AsyncIORegisterService register_service,
    Anything service_context,
    AsyncIOInitService init_service,
    AsyncIORequestService request_service
)
{
    match_result(AsyncIOCode, register_service(service_context),
        success_do_nothing(),
        error(code, message)
        {
            return Error(AsyncIOCode, message, code);
        }
    );

    match_result(FiberCode, init_service((FiberPtr) self, service_context),
        success_do_nothing(),
        error(code, message)
        {
            return Error(AsyncIOCode, message, code);
        }
    );

    return Success(AsyncIOCode, ASYNC_IO_WAIT_COMPLETE);
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_register_filesystem(
    ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    TEST_ASSERT_TRUE(BString__equals(device_path, &expected_device_path));
    TEST_ASSERT_EQUAL_STRING("fat32", BString__cstr(filesystem_type));
    TEST_ASSERT_EQUAL_STRING("/", BString__cstr(mountpoint));
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    TEST_ASSERT_EQUAL_STRING("/", BString__cstr(name));
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    return SERVICE_ABORT;
}

ServiceCode service_filesystem_get_request(uint64_t request_id, ServiceFilesystemRequestPtr request)
{
    if (has_next_filesystem_request && next_filesystem_request.request_id == request_id)
    {
        *request = next_filesystem_request;
        return SERVICE_OK;
    }

    return SERVICE_FILESYSTEM_REQUEST_NOT_FOUND;
}

ServiceCode service_filesystem_complete(uint64_t request_id, ServiceCode status, int64_t result)
{
    last_filesystem_completion = ServiceFilesystemCompletion__new(request_id, status, result);
    return status;
}

ServiceCode service_filesystem_complete_open(
    uint64_t request_id, ServiceCode status, int64_t result, FileMode mode, uint64_t size, uint32_t * blocks
)
{
    last_filesystem_completion = ServiceFilesystemCompletion__new(request_id, status, result);
    last_filesystem_completion.mode = mode;
    last_filesystem_completion.size = size;
    if (blocks != 0)
    {
        for (size_t i = 0; i < 12; i++)
        {
            last_filesystem_completion.block[i] = blocks[i];
        }
    }
    return status;
}

ServiceCode service_filesystem_complete_open_without_blocks(
    uint64_t request_id, ServiceCode status, int64_t result, FileMode mode, uint64_t size
)
{
    last_filesystem_completion = ServiceFilesystemCompletion__new(request_id, status, result);
    last_filesystem_completion.mode = mode;
    last_filesystem_completion.size = size;
    return status;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

static void assert_fat32fs_mounts(Fat32fsVolumePtr volume)
{
    match_result(Fat32fsCode, Fat32fsVolume__mount(volume),
        success(code)
        {
            TEST_ASSERT_EQUAL(FAT32FS_OK, code);
            volume->use_async_io = true;
        },
        error(code, message) { TEST_FAIL_MESSAGE(BString__cstr(&message)); }
    );
}

void test_fat32fs_main_registers_filesystem_after_mount(void)
{
    char * argv[] = {"fat32fs", "/dev/test", "/"};

    TEST_ASSERT_EQUAL_INT(0, fat32fs_main_under_test(3, argv));
}

void test_fat32fs_handles_list_dir_request(void)
{
    Fat32fsVolume volume = {
        .device_path = expected_device_path,
    };
    DirectoryEntry entries[4] = { 0 };
    assert_fat32fs_mounts(&volume);

    next_filesystem_request = ServiceFilesystemRequest__new(
        42,
        SERVICE_FILESYSTEM_LIST_DIR,
        &expected_device_path,
        &bstr("/"),
        0,
        0,
        0,
        0,
        0,
        0,
        (Address) entries,
        4
    );
    has_next_filesystem_request = true;

    Fat32fs__handle_request(&volume, 42);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(2, last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("DOCS", entries[1].name);
}

void test_fat32fs_handles_open_and_read_request(void)
{
    Fat32fsVolume volume = {
        .device_path = expected_device_path,
    };
    char buffer[6] = { 0 };
    assert_fat32fs_mounts(&volume);

    next_filesystem_request = ServiceFilesystemRequest__new(
        43,
        SERVICE_FILESYSTEM_OPEN,
        &expected_device_path,
        &bstr("HELLO.TXT"),
        0,
        IO_OPEN_READ,
        0,
        0,
        0,
        0,
        0,
        0
    );
    has_next_filesystem_request = true;

    Fat32fs__handle_request(&volume, 43);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_FILE | 0644, last_filesystem_completion.mode);
    TEST_ASSERT_EQUAL_UINT64(12, last_filesystem_completion.size);
    TEST_ASSERT_EQUAL_UINT32(3, last_filesystem_completion.block[0]);

    next_filesystem_request = ServiceFilesystemRequest__new(
        44,
        SERVICE_FILESYSTEM_READ,
        &expected_device_path,
        &bstr(""),
        7,
        IO_OPEN_READ,
        last_filesystem_completion.block[0],
        0,
        (Address) buffer,
        5,
        0,
        0
    );

    Fat32fs__handle_request(&volume, 44);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(5, last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_STRING("hello", buffer);
}

void test_fat32fs_handles_write_request_creating_file(void)
{
    Fat32fsVolume volume = {
        .device_path = expected_device_path,
    };
    char content[] = "new data";
    assert_fat32fs_mounts(&volume);

    next_filesystem_request = ServiceFilesystemRequest__new(
        45,
        SERVICE_FILESYSTEM_WRITE,
        &expected_device_path,
        &bstr("NEW.TXT"),
        7,
        IO_OPEN_WRITE,
        0,
        0,
        (Address) content,
        strlen(content),
        0,
        0
    );
    has_next_filesystem_request = true;

    Fat32fs__handle_request(&volume, 45);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(strlen(content), last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_FILE | 0644, last_filesystem_completion.mode);
    TEST_ASSERT_EQUAL_UINT64(strlen(content), last_filesystem_completion.size);
    TEST_ASSERT_EQUAL_STRING_LEN(content, disk[Fat32fsVolume__cluster_sector(&volume, last_filesystem_completion.block[0])], strlen(content));
}

void test_fat32fs_handles_mkdir_and_remove_requests(void)
{
    Fat32fsVolume volume = {
        .device_path = expected_device_path,
    };
    assert_fat32fs_mounts(&volume);

    next_filesystem_request = ServiceFilesystemRequest__new(
        46,
        SERVICE_FILESYSTEM_MKDIR,
        &expected_device_path,
        &bstr("NEWDIR"),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
    );
    has_next_filesystem_request = true;

    Fat32fs__handle_request(&volume, 46);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(IO_OK, last_filesystem_completion.result);

    DirectoryEntry entries[4] = { 0 };
    TEST_ASSERT_EQUAL_INT64(3, Fat32fsVolume__list_dir(&volume, &bstr("/"), entries, 4));
    TEST_ASSERT_EQUAL_STRING("NEWDIR", entries[2].name);

    next_filesystem_request = ServiceFilesystemRequest__new(
        47,
        SERVICE_FILESYSTEM_REMOVE,
        &expected_device_path,
        &bstr("NEWDIR"),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
    );

    Fat32fs__handle_request(&volume, 47);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(IO_OK, last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_INT64(2, Fat32fsVolume__list_dir(&volume, &bstr("/"), entries, 4));
}

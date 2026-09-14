#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sys/mem.h"

#include "../../../../common_includes/syscalls/service.h"

#define FAT32FS_ENTRYPOINT fat32fs_main_under_test
#include "../../../../user/drivers/fat32fs/main.c"

#define TEST_FAT32FS_SECTOR_COUNT 64U

static uint8_t disk[TEST_FAT32FS_SECTOR_COUNT][FAT32FS_BYTES_PER_SECTOR];
static BString expected_device_path;

static void write_u16(uint8_t * data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
}

static void write_u32(uint8_t * data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xff);
    data[offset + 1] = (uint8_t)((value >> 8) & 0xff);
    data[offset + 2] = (uint8_t)((value >> 16) & 0xff);
    data[offset + 3] = (uint8_t)((value >> 24) & 0xff);
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
        entry[out++] = (uint8_t)name[in++];
    }
    if (name[in] == '.')
    {
        in++;
        out = 8;
        while (name[in] != '\0' && out < 11)
        {
            entry[out++] = (uint8_t)name[in++];
        }
    }
}

static void write_dirent(uint8_t * sector, size_t offset, const char * name, uint8_t attr, uint32_t cluster,
                         uint32_t size)
{
    write_short_name(&sector[offset], name);
    sector[offset + 11] = attr;
    write_u16(sector, offset + 20, (uint16_t)((cluster >> 16) & 0xffff));
    write_u16(sector, offset + 26, (uint16_t)(cluster & 0xffff));
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
    build_test_disk();
}

void tearDown(void)
{
}

ChannelCode BlockClient__close(BlockClientPtr self)
{
    return CHANNEL_OK;
}

Result(BlockClient) BlockClient__open(ConstBStringPtr device_path)
{
    TEST_ASSERT_EQUAL_BSTRING(*device_path, expected_device_path);
    return Success(BlockClient, (BlockClient){0});
}

Result(BlockProtocolBlockCount)
    BlockClient__read(BlockClientPtr self, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size)
{
    TEST_ASSERT_EQUAL_UINT64(1, block_count);
    TEST_ASSERT_EQUAL_UINT64(FAT32FS_BYTES_PER_SECTOR, buffer_size);
    TEST_ASSERT_LESS_THAN_UINT64(TEST_FAT32FS_SECTOR_COUNT, block);
    memcpy((void *)buffer, disk[block], FAT32FS_BYTES_PER_SECTOR);
    return Success(BlockProtocolBlockCount, block_count);
}

Result(BlockProtocolBlockCount)
    BlockClient__write(BlockClientPtr self, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size)
{
    TEST_ASSERT_EQUAL_UINT64(1, block_count);
    TEST_ASSERT_EQUAL_UINT64(FAT32FS_BYTES_PER_SECTOR, buffer_size);
    TEST_ASSERT_LESS_THAN_UINT64(TEST_FAT32FS_SECTOR_COUNT, block);
    memcpy(disk[block], (void *)buffer, FAT32FS_BYTES_PER_SECTOR);
    return Success(BlockProtocolBlockCount, block_count);
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

int FilesystemDriver__run(FilesystemDriverPtr self, ConstBStringPtr mountpoint)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), *mountpoint);
    TEST_ASSERT_EQUAL_UINT64(0, FilesystemFiles__len(&self->files));
    return 0;
}

static IORequest request_for(IORequestOperation operation, IOHandle handle, ConstBStringPtr path)
{
    IORequest request = IORequest__new(operation, handle);
    request.client_pid = 42;
    request.client_session = 100;
    request.count = BString__copy_to(path, (char *)request.data, sizeof(request.data));
    return request;
}

static FilesystemDriver mounted_driver(Fat32fsVolumePtr volume)
{
    match_result(Fat32fsVolume__mount(volume),
        success_do_nothing(),
        error_test_fail()
    );
    FilesystemOperations operations = Fat32fs__operations();
    return FilesystemDriver__new(volume, &operations);
}

void test_fat32fs_main_starts_channel_filesystem(void)
{
    char * argv[] = {"fat32fs", "/dev/test", "/"};
    TEST_ASSERT_EQUAL_INT(0, fat32fs_main_under_test(3, argv));
}

void test_fat32fs_lists_directory_with_channel_pagination(void)
{
    Fat32fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_LIST_DIR, 0, &bstr("/"));
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(1, reply.value);
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", reply.entry.name);
    request.offset = 1;
    reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_STRING("DOCS", reply.entry.name);
    request.offset = 2;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
}

void test_fat32fs_owns_multiple_handles_and_offsets(void)
{
    Fat32fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_READ;
    IOReply first = FilesystemDriver__execute(&driver, &request);
    IOReply second = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, first.status);
    TEST_ASSERT_EQUAL(IO_OK, second.status);
    TEST_ASSERT_NOT_EQUAL(first.value, second.value);
    request = request_for(IO_REQUEST_READ, first.value, &empty_bstr());
    request.count = 5;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(5, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("hello", reply.data, 5);
    reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_MEMORY(" fat3", reply.data, 5);
    request.client_pid++;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, FilesystemDriver__execute(&driver, &request).status);
    request.client_pid--;
    request.client_session++;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, FilesystemDriver__execute(&driver, &request).status);
    request.client_session--;
    request.handle = second.value;
    reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_MEMORY("hello", reply.data, 5);
    request.operation = IO_REQUEST_WRITE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.operation = IO_REQUEST_CLOSE;
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, FilesystemDriver__execute(&driver, &request).status);
    TEST_ASSERT_TRUE(FilesystemDriver__release(&driver, 100, 42));
    TEST_ASSERT_EQUAL(0, FilesystemFiles__len(&driver.files));
    TEST_ASSERT_TRUE(FilesystemDriver__release(&driver, 100, 42));
}

void test_fat32fs_writes_chunks_and_reopens_file(void)
{
    Fat32fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/NEW.TXT"));
    request.flags = IO_OPEN_WRITE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_WRITE, opened.value, &bstr("new "));
    TEST_ASSERT_EQUAL(4, FilesystemDriver__execute(&driver, &request).value);
    request = request_for(IO_REQUEST_WRITE, opened.value, &bstr("data"));
    TEST_ASSERT_EQUAL(4, FilesystemDriver__execute(&driver, &request).value);
    request.operation = IO_REQUEST_CLOSE;
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/NEW.TXT"));
    request.flags = IO_OPEN_READ;
    opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_READ, opened.value, &empty_bstr());
    request.count = 256;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(8, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("new data", reply.data, 8);
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
    TEST_ASSERT_TRUE(FilesystemDriver__release(&driver, 100, 42));
}

void test_fat32fs_creates_and_removes_directory(void)
{
    Fat32fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_MKDIR, 0, &bstr("/NEWDIR"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_LIST_DIR, 0, &bstr("/"));
    request.offset = 2;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_STRING("NEWDIR", reply.entry.name);
    request = request_for(IO_REQUEST_REMOVE, 0, &bstr("/NEWDIR"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_LIST_DIR, 0, &bstr("/"));
    request.offset = 2;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
}

void test_fat32fs_preserves_two_full_write_chunks_and_rejects_oversized_requests(void)
{
    Fat32fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/CHUNKS.TXT"));
    request.flags = IO_OPEN_WRITE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_WRITE, opened.value, &empty_bstr());
    request.count = IO_PROTOCOL_DATA_SIZE + 1;
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, FilesystemDriver__execute(&driver, &request).status);
    request.count = IO_PROTOCOL_DATA_SIZE;
    memset(request.data, 'a', sizeof(request.data));
    TEST_ASSERT_EQUAL(256, FilesystemDriver__execute(&driver, &request).value);
    memset(request.data, 'b', sizeof(request.data));
    TEST_ASSERT_EQUAL(256, FilesystemDriver__execute(&driver, &request).value);
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/CHUNKS.TXT"));
    request.flags = IO_OPEN_READ;
    opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_READ, opened.value, &empty_bstr());
    request.count = IO_PROTOCOL_DATA_SIZE;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(256, reply.value);
    TEST_ASSERT_EQUAL_CHAR('a', reply.data[0]);
    TEST_ASSERT_EQUAL_CHAR('a', reply.data[255]);
    reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(256, reply.value);
    TEST_ASSERT_EQUAL_CHAR('b', reply.data[0]);
    TEST_ASSERT_EQUAL_CHAR('b', reply.data[255]);
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
    TEST_ASSERT_TRUE(FilesystemDriver__release(&driver, 100, 42));
}

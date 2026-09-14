#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sys/mem.h"

#include "../../../../common_includes/syscalls/service.h"

#define EXT2FS_ENTRYPOINT ext2fs_main_under_test
#include "../../../../user/drivers/ext2fs/main.c"

#define TEST_EXT2FS_BLOCK_COUNT 16U

static uint8_t disk[TEST_EXT2FS_BLOCK_COUNT][EXT2FS_BLOCK_SIZE];
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

static void write_inode(uint32_t inode_number, FileMode mode, uint32_t size, uint32_t first_block)
{
    uint32_t inode_offset = (inode_number - 1U) * 128U;
    uint32_t block = 5U + inode_offset / EXT2FS_BLOCK_SIZE;
    uint32_t offset = inode_offset % EXT2FS_BLOCK_SIZE;

    write_u16(disk[block], offset, mode);
    write_u32(disk[block], offset + 4U, size);
    write_u16(disk[block], offset + 26U, (mode & EXT2FS_MODE_DIR) != 0 ? 2 : 1);
    write_u32(disk[block], offset + 40U, first_block);
}

static void write_dirent(uint8_t * block, size_t offset, uint32_t inode, uint16_t rec_len, uint8_t type,
                         const char * name)
{
    uint8_t name_len = (uint8_t)strlen(name);
    write_u32(block, offset, inode);
    write_u16(block, offset + 4U, rec_len);
    block[offset + 6U] = name_len;
    block[offset + 7U] = type;
    memcpy(&block[offset + 8U], name, name_len);
}

static void build_test_disk(void)
{
    memset(disk, 0, sizeof(disk));

    write_u32(disk[1], 4U, TEST_EXT2FS_BLOCK_COUNT);
    write_u32(disk[1], 12U, 3);
    write_u32(disk[1], 16U, 4);
    write_u32(disk[1], 20U, 0);
    write_u32(disk[1], 24U, 0);
    write_u32(disk[1], 32U, TEST_EXT2FS_BLOCK_COUNT);
    write_u32(disk[1], 40U, 8);
    write_u16(disk[1], 56U, EXT2FS_SUPER_MAGIC);
    write_u16(disk[1], 88U, 128);

    write_u32(disk[2], 0U, 3);
    write_u32(disk[2], 4U, 4);
    write_u32(disk[2], 8U, 5);
    write_u16(disk[2], 12U, 3);
    write_u16(disk[2], 14U, 4);
    write_u16(disk[2], 16U, 2);

    for (uint32_t i = 0; i <= 12; i++)
    {
        disk[3][i / 8U] = (uint8_t)(disk[3][i / 8U] | (1U << (i % 8U)));
    }
    for (uint32_t i = 0; i < 4; i++)
    {
        disk[4][i / 8U] = (uint8_t)(disk[4][i / 8U] | (1U << (i % 8U)));
    }

    write_inode(EXT2FS_ROOT_INODE, EXT2FS_MODE_DIR | 0755, EXT2FS_BLOCK_SIZE, 10);
    write_inode(3, EXT2FS_MODE_FILE | 0644, 12, 12);
    write_inode(4, EXT2FS_MODE_DIR | 0755, EXT2FS_BLOCK_SIZE, 11);
    memcpy(disk[12], "hello world\n", 12);

    write_dirent(disk[10], 0U, EXT2FS_ROOT_INODE, 12U, EXT2FS_DIRENT_TYPE_DIR, ".");
    write_dirent(disk[10], 12U, EXT2FS_ROOT_INODE, 12U, EXT2FS_DIRENT_TYPE_DIR, "..");
    write_dirent(disk[10], 24U, 3U, 20U, 1U, "HELLO.TXT");
    write_dirent(disk[10], 44U, 4U, EXT2FS_BLOCK_SIZE - 44U, EXT2FS_DIRENT_TYPE_DIR, "DOCS");
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
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, block_count);
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_BLOCK_SIZE, buffer_size);

    uint64_t ext2_block = block / EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
    TEST_ASSERT_LESS_THAN_UINT64(TEST_EXT2FS_BLOCK_COUNT, ext2_block);
    memcpy((void *)buffer, disk[ext2_block], EXT2FS_BLOCK_SIZE);
    return Success(BlockProtocolBlockCount, block_count);
}

Result(BlockProtocolBlockCount)
    BlockClient__write(BlockClientPtr self, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size)
{
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, block_count);
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_BLOCK_SIZE, buffer_size);

    uint64_t ext2_block = block / EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
    TEST_ASSERT_LESS_THAN_UINT64(TEST_EXT2FS_BLOCK_COUNT, ext2_block);
    memcpy(disk[ext2_block], (void *)buffer, EXT2FS_BLOCK_SIZE);
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

static void assert_ext2fs_code_success(Result(Ext2fsCode) result)
{
    match_result(
        result,
        success(code) { TEST_ASSERT_EQUAL(EXT2FS_OK, code); },
        error(code, message) { TEST_FAIL_MESSAGE(BString__cstr(&message)); }
    );
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

static FilesystemDriver mounted_driver(Ext2fsVolumePtr volume)
{
    match_result(Ext2fsVolume__mount(volume),
        success_do_nothing(),
        error_test_fail()
    );
    FilesystemOperations operations = Ext2fs__operations();
    return FilesystemDriver__new(volume, &operations);
}

void test_ext2fs_main_starts_channel_filesystem(void)
{
    char * argv[] = {"ext2fs", "/dev/test", "/"};
    TEST_ASSERT_EQUAL_INT(0, ext2fs_main_under_test(3, argv));
}

void test_ext2fs_lists_directory_with_channel_pagination(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
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

void test_ext2fs_owns_multiple_handles_and_offsets(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
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
    TEST_ASSERT_EQUAL_MEMORY(" worl", reply.data, 5);
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

void test_ext2fs_writes_chunks_and_reopens_file(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
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

void test_ext2fs_creates_and_removes_directory(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
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

void test_ext2fs_preserves_two_full_write_chunks_and_rejects_oversized_requests(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
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

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/service.h"

#define EXT2FS_ENTRYPOINT ext2fs_main_under_test
#include "../../../../user/drivers/ext2fs/main.c"

#define TEST_EXT2FS_BLOCK_COUNT 16U

static uint8_t disk[TEST_EXT2FS_BLOCK_COUNT][EXT2FS_BLOCK_SIZE];
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

static void write_dirent(uint8_t * block, size_t offset, uint32_t inode, uint16_t rec_len, uint8_t type, const char * name)
{
    uint8_t name_len = (uint8_t) strlen(name);
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
        disk[3][i / 8U] = (uint8_t) (disk[3][i / 8U] | (1U << (i % 8U)));
    }
    for (uint32_t i = 0; i < 4; i++)
    {
        disk[4][i / 8U] = (uint8_t) (disk[4][i / 8U] | (1U << (i % 8U)));
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
    has_next_filesystem_request = false;
    last_filesystem_completion = (ServiceFilesystemCompletion) { 0 };
    build_test_disk();
}

void tearDown(void)
{
}

ServiceBlockReadResult service_block_read_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    TEST_ASSERT_TRUE(BString__equals(path, &expected_device_path));
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, block_count);
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_BLOCK_SIZE, buffer_size);

    uint64_t ext2_block = block / EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
    TEST_ASSERT_LESS_THAN_UINT64(TEST_EXT2FS_BLOCK_COUNT, ext2_block);
    memcpy((void *) buffer, disk[ext2_block], EXT2FS_BLOCK_SIZE);
    return block_count;
}

ServiceBlockReadResult service_block_write_device(
    ConstBStringPtr path, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size
)
{
    TEST_ASSERT_TRUE(BString__equals(path, &expected_device_path));
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK, block_count);
    TEST_ASSERT_EQUAL_UINT64(EXT2FS_BLOCK_SIZE, buffer_size);

    uint64_t ext2_block = block / EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
    TEST_ASSERT_LESS_THAN_UINT64(TEST_EXT2FS_BLOCK_COUNT, ext2_block);
    memcpy(disk[ext2_block], (void *) buffer, EXT2FS_BLOCK_SIZE);
    return block_count;
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_register_filesystem(
    ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
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

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

void test_ext2fs_lists_root_directory_from_block_device(void)
{
    Ext2fsVolume volume = (Ext2fsVolume) {
        .device_path = expected_device_path,
    };
    BString root = bstr("/");
    DirectoryEntry entries[4] = { 0 };

    TEST_ASSERT_TRUE(Ext2fsVolume__mount(&volume));
    TEST_ASSERT_EQUAL_INT64(2, Ext2fsVolume__list_dir(&volume, &root, entries, 4));
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("DOCS", entries[1].name);
    TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_FILE | 0644, entries[0].mode);
    TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_DIR | 0755, entries[1].mode);
}

void test_ext2fs_main_registers_filesystem_after_mount(void)
{
    char * argv[] = {"ext2fs", "/dev/test", "/"};

    TEST_ASSERT_EQUAL_INT(0, ext2fs_main_under_test(3, argv));
}

void test_ext2fs_handles_open_request_with_direct_blocks(void)
{
    Ext2fsVolume volume = (Ext2fsVolume) {
        .device_path = expected_device_path,
    };
    TEST_ASSERT_TRUE(Ext2fsVolume__mount(&volume));

    next_filesystem_request = ServiceFilesystemRequest__new(
        42,
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

    Ext2fs__handle_request(&volume, 42);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(0, last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_UINT16(EXT2FS_MODE_FILE | 0644, last_filesystem_completion.mode);
    TEST_ASSERT_EQUAL_UINT64(12, last_filesystem_completion.size);
    TEST_ASSERT_EQUAL_UINT32(12, last_filesystem_completion.block[0]);
}

void test_ext2fs_handles_read_request_into_mapped_buffer(void)
{
    Ext2fsVolume volume = (Ext2fsVolume) {
        .device_path = expected_device_path,
    };
    char buffer[6] = { 0 };
    TEST_ASSERT_TRUE(Ext2fsVolume__mount(&volume));

    next_filesystem_request = ServiceFilesystemRequest__new(
        43,
        SERVICE_FILESYSTEM_READ,
        &expected_device_path,
        &bstr(""),
        7,
        IO_OPEN_READ,
        12,
        0,
        (Address) buffer,
        5,
        0,
        0
    );
    has_next_filesystem_request = true;

    Ext2fs__handle_request(&volume, 43);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(5, last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_STRING("hello", buffer);
}

void test_ext2fs_handles_write_request_creating_file(void)
{
    Ext2fsVolume volume = (Ext2fsVolume) {
        .device_path = expected_device_path,
    };
    char content[] = "new data";
    TEST_ASSERT_TRUE(Ext2fsVolume__mount(&volume));

    next_filesystem_request = ServiceFilesystemRequest__new(
        44,
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

    Ext2fs__handle_request(&volume, 44);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(strlen(content), last_filesystem_completion.result);
    TEST_ASSERT_EQUAL_UINT16(EXT2FS_MODE_FILE | 0644, last_filesystem_completion.mode);
    TEST_ASSERT_EQUAL_UINT64(strlen(content), last_filesystem_completion.size);
    TEST_ASSERT_EQUAL_STRING_LEN(content, disk[last_filesystem_completion.block[0]], strlen(content));

    DirectoryEntry entries[4] = { 0 };
    TEST_ASSERT_EQUAL_INT64(3, Ext2fsVolume__list_dir(&volume, &bstr("/"), entries, 4));
    TEST_ASSERT_EQUAL_STRING("NEW.TXT", entries[2].name);
}

void test_ext2fs_handles_mkdir_request_creating_directory(void)
{
    Ext2fsVolume volume = (Ext2fsVolume) {
        .device_path = expected_device_path,
    };
    TEST_ASSERT_TRUE(Ext2fsVolume__mount(&volume));

    next_filesystem_request = ServiceFilesystemRequest__new(
        45,
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

    Ext2fs__handle_request(&volume, 45);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(IO_OK, last_filesystem_completion.result);

    DirectoryEntry entries[4] = { 0 };
    TEST_ASSERT_EQUAL_INT64(3, Ext2fsVolume__list_dir(&volume, &bstr("/"), entries, 4));
    TEST_ASSERT_EQUAL_STRING("NEWDIR", entries[2].name);
    TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_DIR | 0755, entries[2].mode);
}

void test_ext2fs_handles_remove_request_deleting_directory(void)
{
    Ext2fsVolume volume = (Ext2fsVolume) {
        .device_path = expected_device_path,
    };
    TEST_ASSERT_TRUE(Ext2fsVolume__mount(&volume));
    TEST_ASSERT_TRUE(Ext2fsVolume__mkdir_at_path(&volume, &bstr("NEWDIR")));

    next_filesystem_request = ServiceFilesystemRequest__new(
        46,
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
    has_next_filesystem_request = true;

    Ext2fs__handle_request(&volume, 46);

    TEST_ASSERT_EQUAL(SERVICE_OK, last_filesystem_completion.status);
    TEST_ASSERT_EQUAL_INT64(IO_OK, last_filesystem_completion.result);

    DirectoryEntry entries[4] = { 0 };
    TEST_ASSERT_EQUAL_INT64(2, Ext2fsVolume__list_dir(&volume, &bstr("/"), entries, 4));
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("DOCS", entries[1].name);
}

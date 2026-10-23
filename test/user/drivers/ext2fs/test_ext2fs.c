#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sys/mem.h"
#include "ipc/trace_protocol.h"

#include "../../../../common_includes/syscalls/service.h"

#define EXT2FS_ENTRYPOINT ext2fs_main_under_test
#include "../../../../user/drivers/ext2fs/main.c"

#define TEST_EXT2FS_BLOCK_COUNT 320U
#define TEST_EXT2FS_BIG_FILE_BLOCKS (EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK + 2U)

static uint8_t disk[TEST_EXT2FS_BLOCK_COUNT][EXT2FS_BLOCK_SIZE];
static BString expected_device_path;
static uint64_t list_calls;
static size_t list_capacity;
static uint64_t block_read_calls;
static uint64_t largest_block_read;
static uint64_t block_reads[TEST_EXT2FS_BLOCK_COUNT];
static IODiagnostics diagnostics;
static bool fail_block_writes;

static void reset_block_read_diagnostics(void)
{
    block_read_calls = 0;
    largest_block_read = 0;
    memset(block_reads, 0, sizeof(block_reads));
}

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

static void write_inode_block(uint32_t inode_number, uint32_t index, uint32_t data_block)
{
    uint32_t inode_offset = (inode_number - 1U) * 128U;
    uint32_t block = 5U + inode_offset / EXT2FS_BLOCK_SIZE;
    uint32_t offset = inode_offset % EXT2FS_BLOCK_SIZE;

    write_u32(disk[block], offset + 40U + index * sizeof(uint32_t), data_block);
}

static uint8_t big_file_byte(uint32_t logical_block)
{
    return (uint8_t)(logical_block % 251U + 1U);
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

    for (uint32_t i = 0; i <= 292; i++)
    {
        disk[3][i / 8U] = (uint8_t)(disk[3][i / 8U] | (1U << (i % 8U)));
    }
    for (uint32_t i = 0; i < 5; i++)
    {
        disk[4][i / 8U] = (uint8_t)(disk[4][i / 8U] | (1U << (i % 8U)));
    }

    write_inode(EXT2FS_ROOT_INODE, EXT2FS_MODE_DIR | 0755, EXT2FS_BLOCK_SIZE, 10);
    write_inode(3, EXT2FS_MODE_FILE | 0644, 12, 12);
    write_inode(4, EXT2FS_MODE_DIR | 0755, EXT2FS_BLOCK_SIZE, 11);
    write_inode(5, EXT2FS_MODE_FILE | 0755, TEST_EXT2FS_BIG_FILE_BLOCKS * EXT2FS_BLOCK_SIZE, 20);
    memcpy(disk[12], "hello world\n", 12);

    for (uint32_t i = 0; i < EXT2FS_NDIR_BLOCKS; i++)
    {
        write_inode_block(5, i, 20U + i);
        memset(disk[20U + i], big_file_byte(i), EXT2FS_BLOCK_SIZE);
    }
    write_inode_block(5, EXT2FS_NDIR_BLOCKS, 32U);
    for (uint32_t i = 0; i < EXT2FS_POINTERS_PER_BLOCK; i++)
    {
        write_u32(disk[32], i * sizeof(uint32_t), 33U + i);
        memset(disk[33U + i], big_file_byte(EXT2FS_NDIR_BLOCKS + i), EXT2FS_BLOCK_SIZE);
    }
    write_inode_block(5, EXT2FS_NDIR_BLOCKS + 1U, 289U);
    write_u32(disk[289], 0, 290U);
    write_u32(disk[290], 0, 291U);
    write_u32(disk[290], sizeof(uint32_t), 292U);
    memset(disk[291], big_file_byte(EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK), EXT2FS_BLOCK_SIZE);
    memset(disk[292], big_file_byte(EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK + 1U), EXT2FS_BLOCK_SIZE);

    write_dirent(disk[10], 0U, EXT2FS_ROOT_INODE, 12U, EXT2FS_DIRENT_TYPE_DIR, ".");
    write_dirent(disk[10], 12U, EXT2FS_ROOT_INODE, 12U, EXT2FS_DIRENT_TYPE_DIR, "..");
    write_dirent(disk[10], 24U, 3U, 20U, 1U, "HELLO.TXT");
    write_dirent(disk[10], 44U, 4U, 16U, EXT2FS_DIRENT_TYPE_DIR, "DOCS");
    write_dirent(disk[10], 60U, 5U, EXT2FS_BLOCK_SIZE - 60U, EXT2FS_DIRENT_TYPE_FILE, "BIG.BIN");
}

void setUp(void)
{
    expected_device_path = bstr("/dev/test");
    build_test_disk();
    reset_block_read_diagnostics();
    diagnostics = (IODiagnostics){0};
    IODiagnostics__attach(&diagnostics);
    fail_block_writes = false;
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
    TEST_ASSERT_EQUAL_UINT64(block_count * EXT2FS_DEVICE_BLOCK_SIZE, buffer_size);

    TEST_ASSERT_LESS_OR_EQUAL_UINT64(TEST_EXT2FS_BLOCK_COUNT * EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK,
                                     block + block_count);
    block_read_calls++;
    uint64_t first_ext2_block = block / EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
    uint64_t last_ext2_block = (block + block_count - 1U) / EXT2FS_DEVICE_BLOCKS_PER_EXT2_BLOCK;
    uint64_t ext2_block_count = last_ext2_block - first_ext2_block + 1U;
    if (ext2_block_count > largest_block_read)
    {
        largest_block_read = ext2_block_count;
    }
    for (uint64_t ext2_block = first_ext2_block; ext2_block <= last_ext2_block; ext2_block++)
    {
        block_reads[ext2_block]++;
    }
    memcpy((void *)buffer, (uint8_t *)disk + block * EXT2FS_DEVICE_BLOCK_SIZE, buffer_size);
    return Success(BlockProtocolBlockCount, block_count);
}

Result(BlockProtocolBlockCount)
    BlockClient__write(BlockClientPtr self, uint64_t block, uint64_t block_count, Address buffer, size_t buffer_size)
{
    if (fail_block_writes)
    {
        return Error(BlockProtocolBlockCount, bstr("test block write failed"), EXT2FS_IO_ERROR);
    }
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

TraceStatus DriverTrace__writef(const char * tag, const char * format, ...)
{
    return TRACE_OK;
}

static void assert_ext2fs_code_success(Result(Ext2fsCode) result)
{
    match_result(
        result,
        success(code)
        {
            TEST_ASSERT_EQUAL(EXT2FS_OK, code);
        },
        error(code, message)
        {
            TEST_FAIL_MESSAGE(BString__cstr(&message));
        }
    );
}

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

Result(Anything) mem_alloc_reclaimable_pages_result(size_t size)
{
    return Error(Anything, bstr("reclaimable allocation unavailable in test"), MEM_ALLOCATION_FAILED);
}

MemStatus mem_free_pages(void * address)
{
    return MEM_OK;
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

static Result(DirectoryEntryCount) counted_list(IOServiceContext context, ConstBStringPtr path,
                                                DirectoryEntryPtr entries, size_t capacity, uint64_t offset)
{
    list_calls++;
    list_capacity = capacity;
    return Ext2fs__list(context, path, entries, capacity, offset);
}

void test_ext2fs_main_starts_channel_filesystem(void)
{
    char * argv[] = {"ext2fs", "/dev/test", "/"};
    TEST_ASSERT_EQUAL_INT(0, ext2fs_main_under_test(3, argv));
}

void test_ext2fs_lists_directory_with_channel_pagination(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    match_result(Ext2fsVolume__mount(&volume),
        success_do_nothing(),
        error_test_fail()
    );
    FilesystemOperations operations = Ext2fs__operations();
    operations.list = counted_list;
    FilesystemDriver driver = FilesystemDriver__new(&volume, &operations);
    list_calls = 0;
    list_capacity = 0;
    IORequest request = request_for(IO_REQUEST_LIST_DIR, 0, &bstr("/"));
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(3, reply.directory.count);
    TEST_ASSERT_EQUAL(1, list_calls);
    TEST_ASSERT_EQUAL(IO_DIRECTORY_BATCH_CAPACITY, list_capacity);
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", reply.directory.entries[0].name);
    TEST_ASSERT_EQUAL_STRING("DOCS", reply.directory.entries[1].name);
    TEST_ASSERT_EQUAL_STRING("BIG.BIN", reply.directory.entries[2].name);
    request.offset = 3;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).directory.count);
    TEST_ASSERT_EQUAL(2, list_calls);
}

void test_ext2fs_reports_file_information_without_channel_pagination(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_FILE_INFO, 0, &bstr("/HELLO.TXT"));
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0644, reply.file_info.mode);
    TEST_ASSERT_EQUAL(12, reply.file_info.size);

    request = request_for(IO_REQUEST_FILE_INFO, 0, &bstr("/DOCS"));
    reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR, reply.file_info.mode & IO_MODE_TYPE_MASK);

    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/DOCS"));
    request.flags = IO_OPEN_READ;
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, FilesystemDriver__execute(&driver, &request).status);
}

void test_ext2fs_reuses_cached_metadata_for_repeated_describe(void)
{
    Ext2fsVolume volume = Ext2fsVolume__new(&expected_device_path);
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    reset_block_read_diagnostics();

    match_result(Ext2fs__describe(&volume, &bstr("/HELLO.TXT")),
        success_do_nothing(),
        error_test_fail()
    );
    uint64_t first_describe_reads = block_read_calls;
    TEST_ASSERT_GREATER_THAN_UINT64(0, first_describe_reads);

    match_result(Ext2fs__describe(&volume, &bstr("/HELLO.TXT")),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(first_describe_reads, block_read_calls);
    IODiagnosticsSnapshot snapshot = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_GREATER_THAN_UINT64(0, snapshot.inode_cache_hits);
    TEST_ASSERT_GREATER_THAN_UINT64(0, snapshot.inode_cache_misses);
}

void test_ext2fs_inode_write_updates_cached_value(void)
{
    Ext2fsVolume volume = Ext2fsVolume__new(&expected_device_path);
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    Ext2fsInode inode = unwrap_result(Ext2fsVolume__read_inode(&volume, 3), (Ext2fsInode){0});
    inode.size = 37;

    assert_ext2fs_code_success(Ext2fsVolume__write_inode(&volume, 3, &inode));
    Ext2fsInode cached = unwrap_result(Ext2fsVolume__read_inode(&volume, 3), (Ext2fsInode){0});
    TEST_ASSERT_EQUAL_UINT32(37, cached.size);
    TEST_ASSERT_EQUAL_UINT32(37, Ext2fs__read_u32(disk[5], 2U * 128U + 4U));
}

void test_ext2fs_failed_inode_write_preserves_cached_value(void)
{
    Ext2fsVolume volume = Ext2fsVolume__new(&expected_device_path);
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    Ext2fsInode original = unwrap_result(Ext2fsVolume__read_inode(&volume, 3), (Ext2fsInode){0});
    Ext2fsInode changed = original;
    changed.size = 37;
    fail_block_writes = true;

    match_result(
        Ext2fsVolume__write_inode(&volume, 3, &changed),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(EXT2FS_IO_ERROR, code);
        }
    );
    Ext2fsInode cached = unwrap_result(Ext2fsVolume__read_inode(&volume, 3), (Ext2fsInode){0});
    TEST_ASSERT_EQUAL_UINT32(original.size, cached.size);
    TEST_ASSERT_EQUAL_UINT32(original.size, Ext2fs__read_u32(disk[5], 2U * 128U + 4U));
}

void test_ext2fs_free_inode_invalidates_cached_value(void)
{
    Ext2fsVolume volume = Ext2fsVolume__new(&expected_device_path);
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    unwrap_result(Ext2fsVolume__read_inode(&volume, 3), (Ext2fsInode){0});

    assert_ext2fs_code_success(Ext2fsVolume__free_inode(&volume, 3));
    match_optional(Ext2fsInodeCache__get(&volume.inode_cache, 3),
        some_test_fail(),
        none_do_nothing()
    );
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

void test_ext2fs_inherited_handle_shares_offset_after_parent_closes(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_READ;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);

    request = request_for(IO_REQUEST_READ, opened.value, &empty_bstr());
    request.count = 5;
    IOReply first_read = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, first_read.status);
    TEST_ASSERT_EQUAL_MEMORY("hello", first_read.data, 5);

    request = request_for(IO_REQUEST_INHERIT_PREPARE, opened.value, &empty_bstr());
    IOReply prepared = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, prepared.status);
    request = request_for(IO_REQUEST_INHERIT_COMMIT, 0, &empty_bstr());
    request.inheritance_token = prepared.value;
    request.inheritance_pid = 43;
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);

    request = request_for(IO_REQUEST_CLOSE, opened.value, &empty_bstr());
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    TEST_ASSERT_EQUAL_UINT64(1, FilesystemFiles__len(&driver.files));

    request = request_for(IO_REQUEST_INHERIT_CLAIM, 0, &empty_bstr());
    request.client_session = 101;
    request.client_pid = 43;
    request.inheritance_token = prepared.value;
    IOReply claimed = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, claimed.status);
    TEST_ASSERT_EQUAL_INT64(opened.value, claimed.value);

    request = request_for(IO_REQUEST_READ, claimed.value, &empty_bstr());
    request.client_session = 101;
    request.client_pid = 43;
    request.count = 5;
    IOReply continued = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, continued.status);
    TEST_ASSERT_EQUAL_MEMORY(" worl", continued.data, 5);
}

void test_ext2fs_requires_execute_permission_and_opens_executable_files(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_READ | IO_OPEN_EXECUTE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, opened.status);
    TEST_ASSERT_EQUAL(0, FilesystemFiles__len(&driver.files));
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/BIG.BIN"));
    request.flags = IO_OPEN_READ | IO_OPEN_EXECUTE;
    opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    TEST_ASSERT_EQUAL(1, FilesystemFiles__len(&driver.files));
    request.flags = IO_OPEN_EXECUTE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
    request.flags = IO_OPEN_WRITE | IO_OPEN_EXECUTE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, FilesystemDriver__execute(&driver, &request).status);
}

void test_ext2fs_streams_across_direct_single_and_double_indirect_boundaries(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    uint8_t data[16];
    FilesystemFileSize direct_boundary = EXT2FS_NDIR_BLOCKS * EXT2FS_BLOCK_SIZE;
    match_result(
        Ext2fs__read(&volume, &bstr("/BIG.BIN"), direct_boundary - 8U, data, sizeof(data)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(data), size);
            TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS - 1U), data, 8U);
            TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS), data + 8U, 8U);
        },
        error_test_fail()
    );

    FilesystemFileSize double_boundary = (EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK) * EXT2FS_BLOCK_SIZE;
    match_result(
        Ext2fs__read(&volume, &bstr("/BIG.BIN"), double_boundary - 8U, data, sizeof(data)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(data), size);
            TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK - 1U), data, 8U);
            TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK), data + 8U, 8U);
        },
        error_test_fail()
    );
}

void test_ext2fs_batches_contiguous_direct_blocks(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    reset_block_read_diagnostics();
    uint8_t data[10U * EXT2FS_BLOCK_SIZE];
    match_result(
        Ext2fs__read(&volume, &bstr("/BIG.BIN"), 0, data, sizeof(data)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(data), size);
            for (uint32_t i = 0; i < 10U; i++)
            {
                TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(i), data + i * EXT2FS_BLOCK_SIZE, EXT2FS_BLOCK_SIZE);
            }
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(10, largest_block_read);
    TEST_ASSERT_EQUAL_UINT64(1, block_reads[20]);
    TEST_ASSERT_EQUAL_UINT64(1, block_reads[29]);
}

void test_ext2fs_caches_single_indirect_block_and_batches_data(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    reset_block_read_diagnostics();
    uint8_t data[4U * EXT2FS_BLOCK_SIZE];
    match_result(
        Ext2fs__read(&volume, &bstr("/BIG.BIN"), EXT2FS_NDIR_BLOCKS * EXT2FS_BLOCK_SIZE, data, sizeof(data)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(data), size);
            for (uint32_t i = 0; i < 4U; i++)
            {
                TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS + i), data + i * EXT2FS_BLOCK_SIZE,
                                             EXT2FS_BLOCK_SIZE);
            }
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(1, block_reads[32]);
    TEST_ASSERT_EQUAL_UINT64(4, largest_block_read);
}

void test_ext2fs_caches_double_indirect_blocks_and_batches_data(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    reset_block_read_diagnostics();
    uint8_t data[2U * EXT2FS_BLOCK_SIZE];
    FilesystemFileSize offset = (EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK) * EXT2FS_BLOCK_SIZE;
    match_result(
        Ext2fs__read(&volume, &bstr("/BIG.BIN"), offset, data, sizeof(data)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(data), size);
            TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK), data,
                                         EXT2FS_BLOCK_SIZE);
            TEST_ASSERT_EACH_EQUAL_UINT8(big_file_byte(EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK + 1U),
                                         data + EXT2FS_BLOCK_SIZE, EXT2FS_BLOCK_SIZE);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(1, block_reads[289]);
    TEST_ASSERT_EQUAL_UINT64(1, block_reads[290]);
    TEST_ASSERT_EQUAL_UINT64(2, largest_block_read);
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

void test_ext2fs_exclusive_create_materializes_an_empty_file_and_rejects_collision(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/EXCLUSIVE.TXT"));
    request.flags = IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE;
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    TEST_ASSERT_EQUAL(IO_ALREADY_EXISTS, FilesystemDriver__execute(&driver, &request).status);

    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/EXCLUSIVE.TXT"));
    request.flags = IO_OPEN_READ;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_READ, opened.value, &empty_bstr());
    request.count = 1;
    TEST_ASSERT_EQUAL_UINT64(0, FilesystemDriver__execute(&driver, &request).value);
}

void test_ext2fs_append_refreshes_size_for_independent_handles(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/APPEND.TXT"));
    request.flags = IO_OPEN_WRITE;
    IOReply first = FilesystemDriver__execute(&driver, &request);
    IOReply second = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, first.status);
    TEST_ASSERT_EQUAL(IO_OK, second.status);

    request = request_for(IO_REQUEST_WRITE, first.value, &bstr("a"));
    request.flags = IO_TRANSFER_APPEND;
    TEST_ASSERT_EQUAL_UINT64(1, FilesystemDriver__execute(&driver, &request).value);
    request.handle = second.value;
    request.data[0] = 'b';
    TEST_ASSERT_EQUAL_UINT64(1, FilesystemDriver__execute(&driver, &request).value);

    request = request_for(IO_REQUEST_SEEK, first.value, &empty_bstr());
    request.flags = IO_SEEK_SET;
    TEST_ASSERT_EQUAL_UINT64(0, FilesystemDriver__execute(&driver, &request).value);
    request = request_for(IO_REQUEST_WRITE, first.value, &bstr("c"));
    request.flags = IO_TRANSFER_APPEND;
    TEST_ASSERT_EQUAL_UINT64(1, FilesystemDriver__execute(&driver, &request).value);

    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/APPEND.TXT"));
    request.flags = IO_OPEN_READ;
    IOReply reader = FilesystemDriver__execute(&driver, &request);
    request = request_for(IO_REQUEST_READ, reader.value, &empty_bstr());
    request.count = 3;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_UINT64(3, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("abc", reply.data, 3);
}

void test_ext2fs_creates_and_removes_directory(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_MKDIR, 0, &bstr("/NEWDIR"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_LIST_DIR, 0, &bstr("/"));
    request.offset = 3;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL_STRING("NEWDIR", reply.directory.entries[0].name);
    request = request_for(IO_REQUEST_REMOVE, 0, &bstr("/NEWDIR"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_LIST_DIR, 0, &bstr("/"));
    request.offset = 3;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).directory.count);
}

void test_ext2fs_creates_hard_link_and_preserves_inode_until_last_name_is_removed(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_LINK, 0, &bstr("/HELLO.TXT"));
    IORequest__set_target_path(&request, &bstr("/HELLO.LINK"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);

    request = request_for(IO_REQUEST_REMOVE, 0, &bstr("/HELLO.TXT"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.LINK"));
    request.flags = IO_OPEN_READ;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_READ, opened.value, &empty_bstr());
    request.count = sizeof("hello world\n") - 1;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(sizeof("hello world\n") - 1, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("hello world\n", reply.data, sizeof("hello world\n") - 1);
}

void test_ext2fs_explicit_truncate_preserves_links_and_other_open_handles(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_WRITE;
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    match_result(
        Ext2fsVolume__inode_for_path(&volume, &bstr("/HELLO.TXT")),
        success(inode)
        {
            TEST_ASSERT_EQUAL_UINT32(12, inode.size);
        },
        error_test_fail()
    );
    request = request_for(IO_REQUEST_LINK, 0, &bstr("/HELLO.TXT"));
    IORequest__set_target_path(&request, &bstr("/ALIAS"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_READ;
    IOReply reader = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, reader.status);
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/ALIAS"));
    request.flags = IO_OPEN_READ | IO_OPEN_TRUNCATE;
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, FilesystemDriver__execute(&driver, &request).status);
    request.flags = IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_TRUNCATE;
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    match_result(
        Ext2fsVolume__inode_for_path(&volume, &bstr("/HELLO.TXT")),
        success(inode)
        {
            TEST_ASSERT_EQUAL_UINT32(0, inode.size);
            TEST_ASSERT_EQUAL_UINT16(2, inode.links_count);
            TEST_ASSERT_EQUAL_UINT32(0, inode.sectors);
        },
        error_test_fail()
    );
    request = request_for(IO_REQUEST_READ, reader.value, &empty_bstr());
    request.count = 12;
    TEST_ASSERT_EQUAL(0, FilesystemDriver__execute(&driver, &request).value);
    TEST_ASSERT_TRUE(FilesystemDriver__release(&driver, 100, 42));
}

void test_ext2fs_failed_truncation_write_preserves_the_existing_inode(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_WRITE | IO_OPEN_TRUNCATE;
    fail_block_writes = true;
    TEST_ASSERT_EQUAL(IO_PANIC, FilesystemDriver__execute(&driver, &request).status);
    fail_block_writes = false;
    match_result(
        Ext2fsVolume__inode_for_path(&volume, &bstr("/HELLO.TXT")),
        success(inode)
        {
            TEST_ASSERT_EQUAL_UINT32(12, inode.size);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0, FilesystemFiles__len(&driver.files));
}

void test_ext2fs_truncation_validates_indirect_blocks_before_changing_the_inode(void)
{
    write_inode(3, EXT2FS_MODE_FILE | 0644, 13U * EXT2FS_BLOCK_SIZE, 12);
    write_u32(disk[5], 2U * 128U + 40U + 12U * sizeof(uint32_t), 13);
    write_u32(disk[13], 0, TEST_EXT2FS_BLOCK_COUNT);
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/HELLO.TXT"));
    request.flags = IO_OPEN_WRITE | IO_OPEN_TRUNCATE;
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, FilesystemDriver__execute(&driver, &request).status);
    match_result(
        Ext2fsVolume__inode_for_path(&volume, &bstr("/HELLO.TXT")),
        success(inode)
        {
            TEST_ASSERT_EQUAL_UINT32(13U * EXT2FS_BLOCK_SIZE, inode.size);
            TEST_ASSERT_EQUAL_UINT32(12, inode.block[0]);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0, FilesystemFiles__len(&driver.files));
}

void test_ext2fs_writes_inside_directories_created_after_a_hard_link(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_LINK, 0, &bstr("/HELLO.TXT"));
    IORequest__set_target_path(&request, &bstr("/HELLO.LINK"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);

    request = request_for(IO_REQUEST_MKDIR, 0, &bstr("/MOVED/"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_MKDIR, 0, &bstr("/MOVED/B"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/MOVED/B/COPY"));
    request.flags = IO_OPEN_WRITE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_WRITE, opened.value, &bstr("copy"));
    IOReply written = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, written.status);
    TEST_ASSERT_EQUAL(4, written.value);
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

void test_ext2fs_writes_new_file_beyond_direct_blocks(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    FilesystemDriver driver = mounted_driver(&volume);
    IORequest request = request_for(IO_REQUEST_OPEN, 0, &bstr("/INDIRECT.BIN"));
    request.flags = IO_OPEN_WRITE;
    IOReply opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);

    const uint32_t chunk_count = (EXT2FS_NDIR_BLOCKS + 1U) * EXT2FS_BLOCK_SIZE / IO_PROTOCOL_DATA_SIZE;
    for (uint32_t chunk = 0; chunk < chunk_count; chunk++)
    {
        request = request_for(IO_REQUEST_WRITE, opened.value, &empty_bstr());
        request.count = IO_PROTOCOL_DATA_SIZE;
        memset(request.data, (int)(chunk + 1U), sizeof(request.data));
        TEST_ASSERT_EQUAL(IO_PROTOCOL_DATA_SIZE, FilesystemDriver__execute(&driver, &request).value);
    }

    request = request_for(IO_REQUEST_OPEN, 0, &bstr("/INDIRECT.BIN"));
    request.flags = IO_OPEN_READ;
    opened = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(IO_OK, opened.status);
    request = request_for(IO_REQUEST_SEEK, opened.value, &empty_bstr());
    request.flags = IO_SEEK_SET;
    request.offset = EXT2FS_NDIR_BLOCKS * EXT2FS_BLOCK_SIZE - 1U;
    TEST_ASSERT_EQUAL(request.offset, FilesystemDriver__execute(&driver, &request).value);
    request.operation = IO_REQUEST_READ;
    request.count = 2;
    IOReply reply = FilesystemDriver__execute(&driver, &request);
    TEST_ASSERT_EQUAL(2, reply.value);
    TEST_ASSERT_EQUAL_UINT8(EXT2FS_NDIR_BLOCKS * EXT2FS_BLOCK_SIZE / IO_PROTOCOL_DATA_SIZE, reply.data[0]);
    TEST_ASSERT_EQUAL_UINT8(EXT2FS_NDIR_BLOCKS * EXT2FS_BLOCK_SIZE / IO_PROTOCOL_DATA_SIZE + 1U, reply.data[1]);

    Ext2fsInode inode;
    match_result(
        Ext2fsVolume__inode_for_path(&volume, &bstr("/INDIRECT.BIN")),
        success(value)
        {
            inode = value;
        },
        error_test_fail()
    );
    uint32_t direct_block = inode.block[0];
    uint32_t indirect_block = inode.block[EXT2FS_NDIR_BLOCKS];
    uint32_t indirect_data_block = Ext2fs__read_u32(disk[indirect_block], 0);
    TEST_ASSERT_TRUE(Ext2fs__bitmap_bit_is_set(disk[3], direct_block));
    TEST_ASSERT_TRUE(Ext2fs__bitmap_bit_is_set(disk[3], indirect_block));
    TEST_ASSERT_TRUE(Ext2fs__bitmap_bit_is_set(disk[3], indirect_data_block));

    request = request_for(IO_REQUEST_REMOVE, 0, &bstr("/INDIRECT.BIN"));
    TEST_ASSERT_EQUAL(IO_OK, FilesystemDriver__execute(&driver, &request).status);
    TEST_ASSERT_FALSE(Ext2fs__bitmap_bit_is_set(disk[3], direct_block));
    TEST_ASSERT_FALSE(Ext2fs__bitmap_bit_is_set(disk[3], indirect_block));
    TEST_ASSERT_FALSE(Ext2fs__bitmap_bit_is_set(disk[3], indirect_data_block));
}

void test_ext2fs_overwrites_across_single_and_double_indirect_blocks(void)
{
    Ext2fsVolume volume = {.device_path = expected_device_path};
    assert_ext2fs_code_success(Ext2fsVolume__mount(&volume));
    FilesystemFileSize boundary = (EXT2FS_NDIR_BLOCKS + EXT2FS_POINTERS_PER_BLOCK) * EXT2FS_BLOCK_SIZE;
    uint8_t replacement[8] = {'r', 'e', 'p', 'l', 'a', 'c', 'e', 'd'};
    match_result(
        Ext2fs__write(&volume, &bstr("/BIG.BIN"), boundary - 4U, replacement, sizeof(replacement)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(replacement), size);
        },
        error_test_fail()
    );
    uint8_t data[8];
    match_result(
        Ext2fs__read(&volume, &bstr("/BIG.BIN"), boundary - 4U, data, sizeof(data)),
        success(size)
        {
            TEST_ASSERT_EQUAL(sizeof(data), size);
            TEST_ASSERT_EQUAL_MEMORY(replacement, data, sizeof(data));
        },
        error_test_fail()
    );
}

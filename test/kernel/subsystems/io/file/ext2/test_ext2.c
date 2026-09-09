#include "unity.h"

#include "../../../../../../kernel/address.h"
#include "../../../../../../kernel/process.h"
#include "../../../../../../kernel/scheduler/scheduler.h"
#include "../../../../../../kernel/subsystems/io/block/memory_block_device.h"
#include "../../../../../../kernel/subsystems/io/file/ext2/ext2.h"
#include "../../../../../../kernel/subsystems/io/file/memfs/memfs.h"
#include "../../../../stubs/panic.h"

#define TEST_EXT2_SECTORS 64
#define EXT2_TEST_INODES 16
#define EXT2_TEST_BLOCKS 32

static MemoryBlockDevice memory_device;
static BlockDevice block_device;
static Ext2Volume volume;
static Data async_page[BLOCK_DEVICE_BLOCK_SIZE];
static BString ext2_type = bstr("ext2");
static BString tmp_memfs_type = bstr("tmp/memfs");

typedef struct FakeUserspaceBlockDevice
{
    BString path;
    ProcessPtr service_process;
    SchedulerPtr scheduler;
    BlockCount block_count;
    BlockDevice block_device;
} FakeUserspaceBlockDevice;

static Result(IOInternalHandle) test_file_open(FilesystemPtr fs, IOStatePtr state, BString path, Flags flags)
{
    return fs->file_io.base_io.open(state, &path, flags);
}

static Result(IOBufferPtr)
    test_file_read(FilesystemPtr fs, IOStatePtr state, IOInternalHandle handle, size_t size, ExternalBuffer target)
{
    return fs->file_io.base_io.read(state, handle, size, &target);
}

static Result(ByteCount)
    test_file_write(FilesystemPtr fs, IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    return fs->file_io.base_io.write(state, handle, data, size);
}

static Result(DirectoryEntryCount)
    test_list_dir(FilesystemPtr fs, IOStatePtr state, BString path, DirectoryEntryPtr entries, size_t max_entries)
{
    return fs->file_io.list_dir(state, &path, entries, max_entries);
}

static Result(IOStatus) test_mkdir(FilesystemPtr fs, IOStatePtr state, BString path)
{
    return fs->file_io.mkdir(state, &path);
}

static Result(IOStatus) test_remove(FilesystemPtr fs, IOStatePtr state, BString path, Flags flags)
{
    return fs->file_io.remove(state, &path, flags);
}

static Result(IOStatus) test_link(FilesystemPtr fs, IOStatePtr state, BString source_path, BString target_path)
{
    return fs->file_io.link(state, &source_path, &target_path);
}

static FilesystemPtr test_ext2_filesystem(void)
{
    FileIO file_io = Ext2FilesystemIO__new();
    return Filesystem__heap_new(&ext2_type, &file_io);
}

static FilesystemPtr test_mem_filesystem(void)
{
    FileIO file_io = MemFilesystemIO__new();
    return Filesystem__heap_new(&tmp_memfs_type, &file_io);
}

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    return Success(UserAddress, ((UserAddress){
                                    .virtual_addr = (Address)async_page,
                                    .kernel_addr = KERNEL_ADDR((Address)async_page, (Address)async_page),
                                    .read = true,
                                    .write = true,
                                    .execute = false,
                                    .valid = true,
                                }));
}

static void write_u16(DataPtr data, size_t offset, uint16_t value)
{
    data[offset] = (Data)(value & 0xff);
    data[offset + 1] = (Data)((value >> 8) & 0xff);
}

static void write_u32(DataPtr data, size_t offset, uint32_t value)
{
    data[offset] = (Data)(value & 0xff);
    data[offset + 1] = (Data)((value >> 8) & 0xff);
    data[offset + 2] = (Data)((value >> 16) & 0xff);
    data[offset + 3] = (Data)((value >> 24) & 0xff);
}

static void set_bitmap_bit(DataPtr data, uint32_t index)
{
    data[index / 8] = (Data)(data[index / 8] | (1U << (index % 8)));
}

static void write_block(Ext2Block block, DataPtr data)
{
    match_result(
        BlockDevice__write(&block_device, block * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, data),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
}

static void write_inode(DataPtr inode_table_block, Ext2InodeNumber inode_number, Ext2InodeMode mode, Ext2FileSize size,
                        Ext2Block block0, uint16_t links_count)
{
    size_t offset = (inode_number - 1) * 128;
    write_u16(inode_table_block, offset, mode);
    write_u32(inode_table_block, offset + 4, size);
    write_u16(inode_table_block, offset + 26, links_count);
    write_u32(inode_table_block, offset + 40, block0);
}

static void write_dirent(DataPtr block, size_t offset, Ext2InodeNumber inode, uint16_t rec_len, const char * name,
                         Ext2DirectoryEntryType file_type)
{
    size_t name_len = 0;
    while (name[name_len] != 0)
    {
        name_len++;
    }

    write_u32(block, offset, inode);
    write_u16(block, offset + 4, rec_len);
    block[offset + 6] = (Data)name_len;
    block[offset + 7] = file_type;
    for (size_t i = 0; i < name_len; i++)
    {
        block[offset + 8 + i] = (Data)name[i];
    }
}

static void build_test_volume(void)
{
    Data superblock[EXT2_BLOCK_SIZE] = {0};
    Data group_desc[EXT2_BLOCK_SIZE] = {0};
    Data block_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data inode_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data inode_table[EXT2_BLOCK_SIZE] = {0};
    Data root_dir[EXT2_BLOCK_SIZE] = {0};
    Data file_block[EXT2_BLOCK_SIZE] = {0};

    write_u32(superblock, 0, EXT2_TEST_INODES);
    write_u32(superblock, 4, EXT2_TEST_BLOCKS);
    write_u32(superblock, 12, 25);
    write_u32(superblock, 16, 13);
    write_u32(superblock, 20, 1);
    write_u32(superblock, 24, 0);
    write_u32(superblock, 32, EXT2_TEST_BLOCKS);
    write_u32(superblock, 40, EXT2_TEST_INODES);
    write_u32(superblock, 76, 1);
    write_u16(superblock, 56, EXT2_SUPER_MAGIC);
    write_u16(superblock, 88, 128);

    write_u32(group_desc, 0, 3);
    write_u32(group_desc, 4, 4);
    write_u32(group_desc, 8, 5);
    write_u16(group_desc, 12, 25);
    write_u16(group_desc, 14, 13);
    write_u16(group_desc, 16, 1);

    set_bitmap_bit(block_bitmap, 0);
    set_bitmap_bit(block_bitmap, 1);
    set_bitmap_bit(block_bitmap, 2);
    set_bitmap_bit(block_bitmap, 3);
    set_bitmap_bit(block_bitmap, 4);
    set_bitmap_bit(block_bitmap, 6);
    set_bitmap_bit(block_bitmap, 7);

    set_bitmap_bit(inode_bitmap, 0);
    set_bitmap_bit(inode_bitmap, 1);
    set_bitmap_bit(inode_bitmap, 2);

    write_inode(inode_table, 2, EXT2_INODE_MODE_DIR | 0755, EXT2_BLOCK_SIZE, 7, 2);
    write_inode(inode_table, 3, EXT2_INODE_MODE_FILE | 0644, 5, 8, 1);

    write_dirent(root_dir, 0, 2, 12, ".", 2);
    write_dirent(root_dir, 12, 2, 12, "..", 2);
    write_dirent(root_dir, 24, 3, EXT2_BLOCK_SIZE - 24, "hello.txt", 1);

    file_block[0] = 'h';
    file_block[1] = 'e';
    file_block[2] = 'l';
    file_block[3] = 'l';
    file_block[4] = 'o';

    write_block(1, superblock);
    write_block(2, group_desc);
    write_block(3, block_bitmap);
    write_block(4, inode_bitmap);
    write_block(5, inode_table);
    write_block(7, root_dir);
    write_block(8, file_block);
}

static void write_inode_for_grouped_volume(DataPtr inode_table_block, Ext2InodeCount inodes_per_group,
                                           Ext2InodeNumber inode_number, Ext2InodeMode mode, Ext2FileSize size,
                                           Ext2Block block0, uint16_t links_count)
{
    size_t offset = ((inode_number - 1U) % inodes_per_group) * 128U;
    write_u16(inode_table_block, offset, mode);
    write_u32(inode_table_block, offset + 4U, size);
    write_u16(inode_table_block, offset + 26U, links_count);
    write_u32(inode_table_block, offset + 40U, block0);
}

static void build_multi_group_inode_test_volume(void)
{
    Data superblock[EXT2_BLOCK_SIZE] = {0};
    Data group_desc[EXT2_BLOCK_SIZE] = {0};
    Data group0_block_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data group0_inode_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data group0_inode_table[EXT2_BLOCK_SIZE] = {0};
    Data group4_block_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data group4_inode_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data group4_inode_table[EXT2_BLOCK_SIZE] = {0};
    Data root_dir[EXT2_BLOCK_SIZE] = {0};
    Data file_block[EXT2_BLOCK_SIZE] = {0};

    write_u32(superblock, 0, 80);
    write_u32(superblock, 4, 40);
    write_u32(superblock, 12, 53);
    write_u32(superblock, 16, 74);
    write_u32(superblock, 20, 1);
    write_u32(superblock, 24, 0);
    write_u32(superblock, 32, 8);
    write_u32(superblock, 40, 16);
    write_u32(superblock, 76, 1);
    write_u16(superblock, 56, EXT2_SUPER_MAGIC);
    write_u16(superblock, 88, 128);

    write_u32(group_desc, 0, 3);
    write_u32(group_desc, 4, 4);
    write_u32(group_desc, 8, 5);

    write_u32(group_desc, 1U * 32U + 0U, 8);
    write_u32(group_desc, 1U * 32U + 4U, 9);
    write_u32(group_desc, 1U * 32U + 8U, 10);

    write_u32(group_desc, 2U * 32U + 0U, 15);
    write_u32(group_desc, 2U * 32U + 4U, 16);
    write_u32(group_desc, 2U * 32U + 8U, 17);

    write_u32(group_desc, 3U * 32U + 0U, 18);
    write_u32(group_desc, 3U * 32U + 4U, 19);
    write_u32(group_desc, 3U * 32U + 8U, 20);

    write_u32(group_desc, 4U * 32U + 0U, 11);
    write_u32(group_desc, 4U * 32U + 4U, 12);
    write_u32(group_desc, 4U * 32U + 8U, 13);

    set_bitmap_bit(group0_block_bitmap, 0);
    set_bitmap_bit(group0_block_bitmap, 1);
    set_bitmap_bit(group0_block_bitmap, 2);
    set_bitmap_bit(group0_block_bitmap, 3);
    set_bitmap_bit(group0_block_bitmap, 4);
    set_bitmap_bit(group0_block_bitmap, 6);

    set_bitmap_bit(group0_inode_bitmap, 0);
    set_bitmap_bit(group0_inode_bitmap, 1);

    set_bitmap_bit(group4_block_bitmap, 3);
    set_bitmap_bit(group4_inode_bitmap, 1);

    write_inode_for_grouped_volume(group0_inode_table, 16, 2, EXT2_INODE_MODE_DIR | 0755, EXT2_BLOCK_SIZE, 7, 2);
    write_inode_for_grouped_volume(group4_inode_table, 16, 66, EXT2_INODE_MODE_FILE | 0644, 5, 14, 1);

    write_dirent(root_dir, 0, 2, 12, ".", 2);
    write_dirent(root_dir, 12, 2, 12, "..", 2);
    write_dirent(root_dir, 24, 66, EXT2_BLOCK_SIZE - 24, "hello.txt", 1);

    file_block[0] = 'h';
    file_block[1] = 'e';
    file_block[2] = 'l';
    file_block[3] = 'l';
    file_block[4] = 'o';

    write_block(1, superblock);
    write_block(2, group_desc);
    write_block(3, group0_block_bitmap);
    write_block(4, group0_inode_bitmap);
    write_block(5, group0_inode_table);
    write_block(7, root_dir);
    write_block(11, group4_block_bitmap);
    write_block(12, group4_inode_bitmap);
    write_block(13, group4_inode_table);
    write_block(14, file_block);
}

static void add_empty_docs_directory(void)
{
    Data inode_table[EXT2_BLOCK_SIZE] = {0};
    Data root_dir[EXT2_BLOCK_SIZE] = {0};
    Data docs_dir[EXT2_BLOCK_SIZE] = {0};
    Data block_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data inode_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data superblock[EXT2_BLOCK_SIZE] = {0};
    Data group_desc[EXT2_BLOCK_SIZE] = {0};

    match_result(
        BlockDevice__read(&block_device, 5 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, inode_table),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 7 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, root_dir),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 3 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, block_bitmap),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 4 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, inode_bitmap),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 1 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, superblock),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 2 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, group_desc),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );

    write_inode(inode_table, 4, EXT2_INODE_MODE_DIR | 0755, EXT2_BLOCK_SIZE, 9, 2);
    write_dirent(root_dir, 24, 4, 16, "docs", 2);
    write_dirent(root_dir, 40, 3, EXT2_BLOCK_SIZE - 40, "hello.txt", 1);
    write_dirent(docs_dir, 0, 4, 12, ".", 2);
    write_dirent(docs_dir, 12, 2, EXT2_BLOCK_SIZE - 12, "..", 2);

    set_bitmap_bit(block_bitmap, 8);
    set_bitmap_bit(inode_bitmap, 3);
    write_u32(superblock, 12, 24);
    write_u32(superblock, 16, 12);
    write_u16(group_desc, 12, 24);
    write_u16(group_desc, 14, 12);
    write_u16(group_desc, 16, 2);

    write_block(1, superblock);
    write_block(2, group_desc);
    write_block(3, block_bitmap);
    write_block(4, inode_bitmap);
    write_block(5, inode_table);
    write_block(7, root_dir);
    write_block(9, docs_dir);
}

static void add_docs_directory_with_readme(void)
{
    Data inode_table[EXT2_BLOCK_SIZE] = {0};
    Data root_dir[EXT2_BLOCK_SIZE] = {0};
    Data docs_dir[EXT2_BLOCK_SIZE] = {0};
    Data block_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data inode_bitmap[EXT2_BLOCK_SIZE] = {0};
    Data superblock[EXT2_BLOCK_SIZE] = {0};
    Data group_desc[EXT2_BLOCK_SIZE] = {0};
    Data readme_block[EXT2_BLOCK_SIZE] = {0};

    match_result(
        BlockDevice__read(&block_device, 5 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, inode_table),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 7 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, root_dir),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 3 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, block_bitmap),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 4 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, inode_bitmap),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 1 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, superblock),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );
    match_result(
        BlockDevice__read(&block_device, 2 * EXT2_BLOCK_SECTORS, EXT2_BLOCK_SECTORS, group_desc),
        success(count) { TEST_ASSERT_EQUAL(EXT2_BLOCK_SECTORS, count); },
        error_test_fail()
    );

    write_inode(inode_table, 4, EXT2_INODE_MODE_DIR | 0755, EXT2_BLOCK_SIZE, 9, 2);
    write_inode(inode_table, 5, EXT2_INODE_MODE_FILE | 0644, 6, 10, 1);
    write_dirent(root_dir, 24, 4, 16, "docs", 2);
    write_dirent(root_dir, 40, 3, EXT2_BLOCK_SIZE - 40, "hello.txt", 1);
    write_dirent(docs_dir, 0, 4, 12, ".", 2);
    write_dirent(docs_dir, 12, 2, 12, "..", 2);
    write_dirent(docs_dir, 24, 5, EXT2_BLOCK_SIZE - 24, "readme.txt", 1);

    readme_block[0] = 'r';
    readme_block[1] = 'e';
    readme_block[2] = 'a';
    readme_block[3] = 'd';
    readme_block[4] = 'm';
    readme_block[5] = 'e';

    set_bitmap_bit(block_bitmap, 8);
    set_bitmap_bit(block_bitmap, 9);
    set_bitmap_bit(inode_bitmap, 3);
    set_bitmap_bit(inode_bitmap, 4);
    write_u32(superblock, 12, 23);
    write_u32(superblock, 16, 11);
    write_u16(group_desc, 12, 23);
    write_u16(group_desc, 14, 11);
    write_u16(group_desc, 16, 2);

    write_block(1, superblock);
    write_block(2, group_desc);
    write_block(3, block_bitmap);
    write_block(4, inode_bitmap);
    write_block(5, inode_table);
    write_block(7, root_dir);
    write_block(9, docs_dir);
    write_block(10, readme_block);
}

void setUp(void)
{
    clear_panic_state();
    memory_device = MemoryBlockDevice__new();
    block_device = MemoryBlockDevice__as_block_device(&memory_device);
    Optional(BlockDevicePtr) device = Some(BlockDevicePtr, &block_device);
    volume = Ext2Volume__new(&device, TEST_EXT2_SECTORS);
    build_test_volume();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_ext2_mounts_and_lists_root_directory(void)
{
    match_result(Ext2Volume__mount(&volume),
        success(code) { TEST_ASSERT_EQUAL(EXT2_OK, code); },
        error_test_fail()
    );

    match_result(
        Ext2Volume__list_root(&volume),
        success(entries)
        {
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_EQUAL_STRING("hello.txt", entries.data[0].name.data);
            TEST_ASSERT_EQUAL_UINT16(EXT2_INODE_MODE_FILE | 0644, entries.data[0].mode);
        },
        error_test_fail()
    );
}

void test_ext2_reads_root_file_contents(void)
{
    Data output[16] = {0};
    ExternalBuffer target = (ExternalBuffer){
        .data = output,
        .len = sizeof(output),
    };

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                test_file_read(fs, &state->state.state, handle, 5, target),
                success(buffer)
                {
                    TEST_ASSERT_EQUAL(5, IOBuffer__item_count(buffer));

                    match_result(
                        IOBuffer__drain(buffer, output, 5),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(5, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("hello", output, 5);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_ext2_reads_root_file_contents_in_chunks(void)
{
    Data output[8] = {0};
    ExternalBuffer target = (ExternalBuffer){
        .data = output,
        .len = sizeof(output),
    };

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                test_file_read(fs, &state->state.state, handle, 2, target),
                success(buffer)
                {
                    TEST_ASSERT_EQUAL(2, IOBuffer__item_count(buffer));
                    match_result(
                        IOBuffer__drain(buffer, output, 2),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(2, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("he", output, 2);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );

            match_result(
                test_file_read(fs, &state->state.state, handle, 3, target),
                success(buffer)
                {
                    TEST_ASSERT_EQUAL(3, IOBuffer__item_count(buffer));
                    match_result(
                        IOBuffer__drain(buffer, output, 3),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(3, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("llo", output, 3);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_ext2_reads_file_contents_from_later_inode_group(void)
{
    Data output[16] = {0};
    ExternalBuffer target = (ExternalBuffer){
        .data = output,
        .len = sizeof(output),
    };

    memory_device = MemoryBlockDevice__new();
    block_device = MemoryBlockDevice__as_block_device(&memory_device);
    Optional(BlockDevicePtr) device = Some(BlockDevicePtr, &block_device);
    volume = Ext2Volume__new(&device, TEST_EXT2_SECTORS);
    build_multi_group_inode_test_volume();

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                test_file_read(fs, &state->state.state, handle, 5, target),
                success(buffer)
                {
                    match_result(
                        IOBuffer__drain(buffer, output, 5),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(5, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("hello", output, 5);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_ext2_list_dir_uses_async_mount_then_root_read_when_unmounted(void)
{
    DirectoryEntry entries[4] = {0};

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success(count)
        {
            TEST_ASSERT_EQUAL(1, count);
            TEST_ASSERT_EQUAL_STRING("hello.txt", entries[0].name);
            TEST_ASSERT_EQUAL_UINT64(IO_DIRENT_ATTR_NONE, entries[0].attributes);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_ROOT, entries[0].user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_ROOT, entries[0].group_id);
            TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_FILE | 0644, entries[0].mode);
        },
        error_test_fail()
    );

    TEST_ASSERT_TRUE(state->volume.mounted);
}

void test_ext2_userspace_open_starts_async_inode_lookup(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
}

void test_root_filesystem_can_mount_ext2_at_root_and_list_mount_overlays(void)
{
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_state = FilesystemIOState__heap_new(root_fs);
    FilesystemPtr ext2_fs = test_ext2_filesystem();
    FilesystemPtr memfs = test_mem_filesystem();
    DirectoryEntry root_entries[4] = {0};
    DirectoryEntry tmp_entries[2] = {0};

    Ext2FilesystemIOState__heap_new(ext2_fs, &block_device, 0);
    MemFilesystemIOState__heap_new(memfs);

    match_result(
        Filesystem__mount(root_fs, &bstr("/"), ext2_fs),
        success(code) { TEST_ASSERT_EQUAL(MOUNT_OK, code); },
        error_test_fail()
    );

    match_result(
        Filesystem__mount(root_fs, &bstr("/tmp/memfs"), memfs),
        success(code) { TEST_ASSERT_EQUAL(MOUNT_OK, code); },
        error_test_fail()
    );

    match_result(
        test_list_dir(root_fs, &root_state->state, bstring("/"), root_entries, 4),
        success(count)
        {
            TEST_ASSERT_EQUAL(2, count);
            TEST_ASSERT_EQUAL_STRING("hello.txt", root_entries[0].name);
            TEST_ASSERT_EQUAL_STRING("tmp", root_entries[1].name);
        },
        error_test_fail()
    );

    match_result(
        test_list_dir(root_fs, &root_state->state, bstring("/tmp"), tmp_entries, 2),
        success(count)
        {
            TEST_ASSERT_EQUAL(1, count);
            TEST_ASSERT_EQUAL_STRING("memfs", tmp_entries[0].name);
        },
        error_test_fail()
    );
}

void test_ext2_userspace_list_dir_starts_async_when_mounted(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    DirectoryEntry entries[4] = {0};
    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
}

void test_ext2_remove_deletes_regular_file_and_frees_root_entry(void)
{
    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_remove(fs, &state->state.state, bstring("hello.txt"), 0),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4] = {0};
    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success(count) { TEST_ASSERT_EQUAL(0, count); },
        error_test_fail()
    );

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_INVALID_PATH, code); }
    );
}

void test_ext2_remove_deletes_empty_directory(void)
{
    add_empty_docs_directory();

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_remove(fs, &state->state.state, bstring("docs"), 0),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4] = {0};
    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success(count)
        {
            TEST_ASSERT_EQUAL(1, count);
            TEST_ASSERT_EQUAL_STRING("hello.txt", entries[0].name);
        },
        error_test_fail()
    );

    match_result(
        test_file_open(fs, &state->state.state, bstring("docs"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_INVALID_PATH, code); }
    );
}

void test_ext2_write_open_creates_new_file_and_reads_back(void)
{
    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);
    Data output[16] = {0};
    ExternalBuffer target = (ExternalBuffer){
        .data = output,
        .len = sizeof(output),
    };

    match_result(
        test_file_open(fs, &state->state.state, bstring("notes.txt"), IO_OPEN_WRITE),
        success(write_handle)
        {
            Data input[] = "woof";
            match_result(
                test_file_write(fs, &state->state.state, write_handle, input, sizeof(input) - 1),
                success(bytes) { TEST_ASSERT_EQUAL(4, bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_result(
        test_file_open(fs, &state->state.state, bstring("notes.txt"), IO_OPEN_READ),
        success(read_handle)
        {
            match_result(
                test_file_read(fs, &state->state.state, read_handle, 4, target),
                success(buffer)
                {
                    match_result(
                        IOBuffer__drain(buffer, output, 4),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(4, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("woof", output, 4);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_ext2_write_open_truncates_existing_file(void)
{
    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);
    Data output[16] = {0};
    ExternalBuffer target = (ExternalBuffer){
        .data = output,
        .len = sizeof(output),
    };

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_WRITE),
        success(write_handle)
        {
            Data input[] = "yo";
            match_result(
                test_file_write(fs, &state->state.state, write_handle, input, sizeof(input) - 1),
                success(bytes) { TEST_ASSERT_EQUAL(2, bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_READ),
        success(read_handle)
        {
            match_result(
                test_file_read(fs, &state->state.state, read_handle, 16, target),
                success(buffer)
                {
                    match_result(
                        IOBuffer__drain(buffer, output, 2),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(2, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("yo", output, 2);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_ext2_mkdir_creates_empty_directory(void)
{
    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);
    DirectoryEntry root_entries[4] = {0};
    DirectoryEntry docs_entries[4] = {0};

    match_result(
        test_mkdir(fs, &state->state.state, bstring("docs")),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), root_entries, 4),
        success(count)
        {
            TEST_ASSERT_EQUAL(2, count);
            TEST_ASSERT_EQUAL_STRING("hello.txt", root_entries[0].name);
            TEST_ASSERT_EQUAL_STRING("docs", root_entries[1].name);
            TEST_ASSERT_EQUAL_UINT64(IO_DIRENT_ATTR_NONE, root_entries[1].attributes);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_ROOT, root_entries[1].user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_ROOT, root_entries[1].group_id);
            TEST_ASSERT_EQUAL_UINT16(IO_MODE_TYPE_DIR | 0755, root_entries[1].mode);
        },
        error_test_fail()
    );

    match_result(
        test_list_dir(fs, &state->state.state, bstring("docs"), docs_entries, 4),
        success(count) { TEST_ASSERT_EQUAL(0, count); },
        error_test_fail()
    );
}

void test_ext2_remove_recursive_deletes_directory_tree(void)
{
    add_docs_directory_with_readme();

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_remove(fs, &state->state.state, bstring("docs"), IO_REMOVE_RECURSIVE),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4] = {0};
    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success(count)
        {
            TEST_ASSERT_EQUAL(1, count);
            TEST_ASSERT_EQUAL_STRING("hello.txt", entries[0].name);
        },
        error_test_fail()
    );

    match_result(
        test_file_open(fs, &state->state.state, bstring("docs/readme.txt"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_INVALID_PATH, code); }
    );
}

void test_ext2_userspace_remove_starts_async_inode_lookup(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_remove(fs, &state->state.state, bstring("hello.txt"), 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
}

void test_ext2_userspace_recursive_remove_starts_async_inode_lookup(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_remove(fs, &state->state.state, bstring("docs"), IO_REMOVE_RECURSIVE),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
}

void test_ext2_userspace_write_starts_async_for_new_file(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_file_open(fs, &state->state.state, bstring("new.txt"), IO_OPEN_WRITE),
        success(handle)
        {
            Data input[] = "hi";
            match_result(
                test_file_write(fs, &state->state.state, handle, input, sizeof(input) - 1),
                success_test_fail(),
                error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
            );
        },
        error_test_fail()
    );
}

void test_ext2_userspace_write_starts_async_for_existing_file(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_file_open(fs, &state->state.state, bstring("hello.txt"), IO_OPEN_WRITE),
        success(handle)
        {
            Data input[] = "yo";
            match_result(
                test_file_write(fs, &state->state.state, handle, input, sizeof(input) - 1),
                success_test_fail(),
                error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
            );
        },
        error_test_fail()
    );
}

void test_ext2_userspace_mkdir_starts_async(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_mkdir(fs, &state->state.state, bstring("docs")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
}

void test_ext2_link_creates_hard_link_and_preserves_file_after_single_remove(void)
{
    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);
    Data output[16] = {0};
    ExternalBuffer target = (ExternalBuffer){
        .data = output,
        .len = sizeof(output),
    };

    match_result(
        test_link(fs, &state->state.state, bstring("hello.txt"), bstring("copy.txt")),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4] = {0};
    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success(count)
        {
            TEST_ASSERT_EQUAL(2, count);
            TEST_ASSERT_EQUAL_STRING("hello.txt", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("copy.txt", entries[1].name);
        },
        error_test_fail()
    );

    match_result(
        test_remove(fs, &state->state.state, bstring("hello.txt"), 0),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    match_result(
        test_file_open(fs, &state->state.state, bstring("copy.txt"), IO_OPEN_READ),
        success(read_handle)
        {
            match_result(
                test_file_read(fs, &state->state.state, read_handle, 5, target),
                success(buffer)
                {
                    match_result(
                        IOBuffer__drain(buffer, output, 5),
                        success(bytes_read)
                        {
                            TEST_ASSERT_EQUAL(5, bytes_read);
                            TEST_ASSERT_EQUAL_MEMORY("hello", output, 5);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_ext2_link_remove_last_link_deletes_file(void)
{
    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &block_device, 0);

    match_result(
        test_link(fs, &state->state.state, bstring("hello.txt"), bstring("copy.txt")),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    match_result(
        test_remove(fs, &state->state.state, bstring("hello.txt"), 0),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );
    match_result(
        test_remove(fs, &state->state.state, bstring("copy.txt"), 0),
        success(status) { TEST_ASSERT_EQUAL(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4] = {0};
    match_result(
        test_list_dir(fs, &state->state.state, bstring("/"), entries, 4),
        success(count) { TEST_ASSERT_EQUAL(0, count); },
        error_test_fail()
    );
}

void test_ext2_userspace_link_starts_async(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)1,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    match_result(
        test_link(fs, &state->state.state, bstring("hello.txt"), bstring("copy.txt")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
}

void test_ext2_userspace_link_preserves_live_kernel_context_on_first_request(void)
{
    FakeUserspaceBlockDevice userspace = {
        .path = bstr("/dev/test"),
        .service_process = (ProcessPtr)1,
        .scheduler = (SchedulerPtr)0x44,
        .block_count = TEST_EXT2_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    FilesystemPtr fs = test_ext2_filesystem();
    Ext2FilesystemIOStatePtr state = Ext2FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->volume.mounted = true;

    KernelContext kernel_context = KernelContext__new(&Some(ProcessID, 77), &Some(SchedulerPtr, userspace.scheduler));
    BlockDevice__set_current_context(&kernel_context);
    match_result(
        test_link(fs, &state->state.state, bstring("hello.txt"), bstring("copy.txt")),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    Optional(KernelContextPtr) current_context = BlockDevice__current_context();
    TEST_ASSERT_FALSE(current_context.present);
}

#include "unity.h"

#include "../../../../../../kernel/address.h"
#include "../../../../../../kernel/process.h"
#include "../../../../../../kernel/scheduler/scheduler.h"
#include "../../../../../../kernel/service/service.h"
#include "../../../../../../kernel/subsystems/io/block/memory_block_device.h"
#include "../../../../../../kernel/subsystems/io/file/fat32/fat32.h"
#include "../../../../stubs/panic.h"

#define TEST_FAT32_SECTORS 256

static MemoryBlockDevice memory_device;
static BlockDevice block_device;
static Fat32Volume volume;
static Data async_page[BLOCK_DEVICE_BLOCK_SIZE];

typedef struct FakeUserspaceBlockDevice
{
    BString path;
    ProcessPtr service_process;
    SchedulerPtr scheduler;
    BlockCount block_count;
    BlockDevice block_device;
}
FakeUserspaceBlockDevice;

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    return Success(UserAddress, ((UserAddress) {
        .virtual_addr = (Address) async_page,
        .kernel_addr = KERNEL_ADDR((Address) async_page, (Address) async_page),
        .read = true,
        .write = true,
        .execute = false,
        .valid = true,
    }));
}

void setUp(void)
{
    clear_panic_state();
    memory_device = MemoryBlockDevice__new();
    block_device = MemoryBlockDevice__as_block_device(&memory_device);
    volume = Fat32Volume__new(Some(BlockDevicePtr, &block_device), TEST_FAT32_SECTORS);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

static void format_volume(void)
{
    ResultMatch(Fat32Code, Fat32Volume__format(&volume))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End
}

static void mount_volume(void)
{
    ResultMatch(Fat32Code, Fat32Volume__mount(&volume))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End
}

static void test_write_u16(DataPtr data, size_t offset, uint16_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
}

static void test_write_u32(DataPtr data, size_t offset, uint32_t value)
{
    data[offset] = (Data) (value & 0xff);
    data[offset + 1] = (Data) ((value >> 8) & 0xff);
    data[offset + 2] = (Data) ((value >> 16) & 0xff);
    data[offset + 3] = (Data) ((value >> 24) & 0xff);
}

static uint8_t test_short_name_checksum(DataPtr short_name)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < 11; i++)
    {
        sum = (uint8_t) ((((sum & 1) << 7) + (sum >> 1) + short_name[i]) & 0xff);
    }
    return sum;
}

static void test_write_fat_entry(uint32_t cluster, uint32_t value)
{
    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    size_t fat_offset = cluster * sizeof(uint32_t);
    BlockNumber relative_sector = fat_offset / FAT32_BYTES_PER_SECTOR;
    size_t sector_offset = fat_offset % FAT32_BYTES_PER_SECTOR;

    for (uint8_t fat = 0; fat < FAT32_FAT_COUNT; fat++)
    {
        BlockNumber fat_sector = volume.fat_start_sector + (fat * volume.sectors_per_fat) + relative_sector;
        ResultMatch(BlockCount, BlockDevice__read(&block_device, fat_sector, 1, sector))
            IsSuccess(count, TEST_ASSERT_EQUAL(1, count))
            IsError(code, message, TEST_FAIL())
        End

        test_write_u32(sector, sector_offset, value);

        ResultMatch(BlockCount, BlockDevice__write(&block_device, fat_sector, 1, sector))
            IsSuccess(count, TEST_ASSERT_EQUAL(1, count))
            IsError(code, message, TEST_FAIL())
        End
    }
}

static void test_write_cluster(uint32_t cluster, DataPtr data, size_t size)
{
    Data sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    for (size_t i = 0; i < size; i++)
    {
        sector[i] = data[i];
    }

    BlockNumber sector_number = volume.data_start_sector + ((cluster - 2) * FAT32_SECTORS_PER_CLUSTER);
    ResultMatch(BlockCount, BlockDevice__write(&block_device, sector_number, 1, sector))
        IsSuccess(count, TEST_ASSERT_EQUAL(1, count))
        IsError(code, message, TEST_FAIL())
    End
}

static void test_write_short_entry(DataPtr entry, DataPtr short_name, uint32_t cluster, uint32_t size)
{
    for (size_t i = 0; i < 11; i++)
    {
        entry[i] = short_name[i];
    }
    entry[11] = FAT32_ATTR_ARCHIVE;
    test_write_u16(entry, 20, (uint16_t) ((cluster >> 16) & 0xffff));
    test_write_u16(entry, 26, (uint16_t) (cluster & 0xffff));
    test_write_u32(entry, 28, size);
}

static void test_write_lfn_entries(DataPtr target, BString name, DataPtr short_name)
{
    uint8_t checksum = test_short_name_checksum(short_name);
    size_t entry_count = (name.len + 1 + 12) / 13;
    size_t positions[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

    for (size_t entry_index = 0; entry_index < entry_count; entry_index++)
    {
        size_t sequence = entry_count - entry_index;
        DataPtr entry = &target[entry_index * FAT32_ROOT_ENTRY_SIZE];
        entry[0] = (Data) sequence;
        if (sequence == entry_count)
        {
            entry[0] |= 0x40;
        }
        entry[11] = FAT32_ATTR_LFN;
        entry[13] = checksum;

        for (size_t char_index = 0; char_index < 13; char_index++)
        {
            size_t name_index = ((sequence - 1) * 13) + char_index;
            uint16_t value = 0xffff;
            if (name_index < name.len)
            {
                value = (uint16_t) name.data[name_index];
            }
            else if (name_index == name.len)
            {
                value = 0;
            }
            test_write_u16(entry, positions[char_index], value);
        }
    }
}

void test_fat32_formats_and_mounts_volume(void)
{
    format_volume();

    Fat32Volume mounted = Fat32Volume__new(Some(BlockDevicePtr, &block_device), 0);
    ResultMatch(Fat32Code, Fat32Volume__mount(&mounted))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(TEST_FAT32_SECTORS, mounted.total_sectors);
    TEST_ASSERT_EQUAL(FAT32_RESERVED_SECTORS, mounted.fat_start_sector);
    TEST_ASSERT_EQUAL(FAT32_ROOT_CLUSTER, mounted.root_cluster);
    TEST_ASSERT_TRUE(mounted.data_start_sector > mounted.fat_start_sector);
}

void test_fat32_mount_rejects_missing_device(void)
{
    Fat32Volume missing_device = Fat32Volume__new(None(BlockDevicePtr), TEST_FAT32_SECTORS);

    ResultMatch(Fat32Code, Fat32Volume__mount(&missing_device))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(FAT32_INVALID_ARGUMENT, code))
    End
}

void test_fat32_filesystem_state_mounts_existing_volume_without_formatting(void)
{
    Data content[] = { 'k', 'e', 'e', 'p' };

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, bstring("keep.txt"), content, sizeof(content)))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    FilesystemPtr fs = Filesystem__heap_new(bstring("fat32"), Fat32FilesystemIO__new());
    Fat32FilesystemIOStatePtr state = Fat32FilesystemIOState__heap_new(fs, &block_device, 0);

    TEST_ASSERT_TRUE(state->mounted);

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&state->volume))
        IsSuccess(entries, {
            BString expected = BSTRING_NEW("KEEP.TXT");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(BString__equals(&entries.data[0].name, &expected));
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_lists_empty_root_directory_after_format(void)
{
    format_volume();
    mount_volume();

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries, TEST_ASSERT_EQUAL(0, entries.len))
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_list_root_ignores_volume_label_entry(void)
{
    Data root_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    Data label[] = { 'K', 'E', 'R', 'N', 'E', 'L', 'F', 'S', ' ', ' ', ' ' };

    format_volume();
    mount_volume();

    for (size_t index = 0; index < sizeof(label); index++)
    {
        root_sector[index] = label[index];
    }
    root_sector[11] = FAT32_ATTR_VOLUME_ID;

    ResultMatch(BlockCount, BlockDevice__write(&block_device, volume.data_start_sector, 1, root_sector))
        IsSuccess(count, TEST_ASSERT_EQUAL(1, count))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries, TEST_ASSERT_EQUAL(0, entries.len))
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_creates_lists_and_reads_root_file(void)
{
    Data content[] = { 'h', 'e', 'l', 'l', 'o' };
    Data target[16] = { 0 };
    uint32_t bytes_read = 0;

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, bstring("hello.txt"), content, sizeof(content)))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries, {
            BString expected = BSTRING_NEW("HELLO.TXT");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(BString__equals(&entries.data[0].name, &expected));
            TEST_ASSERT_EQUAL(FAT32_ATTR_ARCHIVE, entries.data[0].attributes);
            TEST_ASSERT_EQUAL(sizeof(content), entries.data[0].size);
            TEST_ASSERT_TRUE(entries.data[0].first_cluster >= 2);
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32Code, Fat32Volume__read_root_file(
        &volume,
        bstring("HELLO.TXT"),
        target,
        sizeof(target),
        &bytes_read
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(sizeof(content), bytes_read);
    TEST_ASSERT_EQUAL_MEMORY(content, target, sizeof(content));
}

void test_fat32_lists_and_reads_long_filename_root_file(void)
{
    BString long_name = BSTRING_NEW("This is a long filename.txt");
    Data short_name[11] = { 'L', 'O', 'N', 'G', 'F', 'I', '~', '1', 'T', 'X', 'T' };
    Data content[] = { 'l', 'o', 'n', 'g', ' ', 'n', 'a', 'm', 'e' };
    Data target[32] = { 0 };
    uint32_t bytes_read = 0;
    Data root_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    size_t lfn_entry_count = (long_name.len + 1 + 12) / 13;
    uint32_t file_cluster = 3;

    format_volume();
    mount_volume();

    test_write_fat_entry(file_cluster, FAT32_END_OF_CHAIN);
    test_write_cluster(file_cluster, content, sizeof(content));
    test_write_lfn_entries(root_sector, long_name, short_name);
    test_write_short_entry(&root_sector[lfn_entry_count * FAT32_ROOT_ENTRY_SIZE], short_name, file_cluster, sizeof(content));

    ResultMatch(BlockCount, BlockDevice__write(&block_device, volume.data_start_sector, 1, root_sector))
        IsSuccess(count, TEST_ASSERT_EQUAL(1, count))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries,
        {
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(BString__equals(&entries.data[0].name, &long_name));
            TEST_ASSERT_EQUAL(sizeof(content), entries.data[0].size);
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32Code, Fat32Volume__read_root_file(&volume, long_name, target, sizeof(target), &bytes_read))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(sizeof(content), bytes_read);
    TEST_ASSERT_EQUAL_MEMORY(content, target, sizeof(content));
}

void test_fat32_reads_root_file_across_cluster_chain(void)
{
    Data short_name[11] = { 'B', 'I', 'G', 'F', 'I', 'L', 'E', ' ', 'T', 'X', 'T' };
    Data first_cluster[FAT32_BYTES_PER_SECTOR] = { 0 };
    Data second_cluster[FAT32_BYTES_PER_SECTOR] = { 0 };
    Data expected[FAT32_BYTES_PER_SECTOR + 64] = { 0 };
    Data target[FAT32_BYTES_PER_SECTOR + 64] = { 0 };
    Data root_sector[FAT32_BYTES_PER_SECTOR] = { 0 };
    uint32_t bytes_read = 0;
    uint32_t file_size = sizeof(expected);

    for (size_t i = 0; i < sizeof(first_cluster); i++)
    {
        first_cluster[i] = (Data) ('A' + (i % 26));
        expected[i] = first_cluster[i];
    }
    for (size_t i = 0; i < 64; i++)
    {
        second_cluster[i] = (Data) ('a' + (i % 26));
        expected[FAT32_BYTES_PER_SECTOR + i] = second_cluster[i];
    }

    format_volume();
    mount_volume();

    test_write_fat_entry(3, 4);
    test_write_fat_entry(4, FAT32_END_OF_CHAIN);
    test_write_cluster(3, first_cluster, sizeof(first_cluster));
    test_write_cluster(4, second_cluster, sizeof(second_cluster));
    test_write_short_entry(root_sector, short_name, 3, file_size);

    ResultMatch(BlockCount, BlockDevice__write(&block_device, volume.data_start_sector, 1, root_sector))
        IsSuccess(count, TEST_ASSERT_EQUAL(1, count))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32Code, Fat32Volume__read_root_file(
        &volume,
        bstring("BIGFILE.TXT"),
        target,
        sizeof(target),
        &bytes_read
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(file_size, bytes_read);
    TEST_ASSERT_EQUAL_MEMORY(expected, target, file_size);
}

void test_fat32_list_dir_uses_async_mount_then_root_read_when_unmounted(void)
{
    Data content[] = { 'a' };
    DirectoryEntry entries[4] = { 0 };

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, bstring("async.txt"), content, sizeof(content)))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    FilesystemPtr fs = Filesystem__heap_new(bstring("fat32"), Fat32FilesystemIO__new());
    Fat32FilesystemIOStatePtr state = Fat32FilesystemIOState__heap_new(fs, &block_device, 0);
    state->mounted = false;
    state->volume = Fat32Volume__new(Some(BlockDevicePtr, &block_device), 0);

    ResultMatch(DirectoryEntryCount, fs->file_io.list_dir(&state->state.state, bstring("/"), entries, 4))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL(1, count);
            TEST_ASSERT_EQUAL_STRING("ASYNC.TXT", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_TRUE(state->mounted);
    TEST_ASSERT_EQUAL(volume.data_start_sector, state->volume.data_start_sector);
    TEST_ASSERT_EQUAL(volume.root_cluster, state->volume.root_cluster);
}

void test_fat32_async_list_dir_resolves_nested_directory(void)
{
    DirectoryEntry entries[4] = { 0 };

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_directory(&volume, bstring("docs")))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    FilesystemPtr fs = Filesystem__heap_new(bstring("fat32"), Fat32FilesystemIO__new());
    Fat32FilesystemIOStatePtr state = Fat32FilesystemIOState__heap_new(fs, &block_device, 0);
    state->mounted = false;
    state->volume = Fat32Volume__new(Some(BlockDevicePtr, &block_device), 0);

    ResultMatch(DirectoryEntryCount, fs->file_io.list_dir(&state->state.state, bstring("docs"), entries, 4))
        IsSuccess(count, TEST_ASSERT_EQUAL(0, count))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_TRUE(state->mounted);
}

void test_fat32_userspace_async_write_does_not_hold_handle_lock(void)
{
    Data content[] = { 'h', 'i' };
    FakeUserspaceBlockDevice userspace = {
        .path = BSTRING_NEW("/dev/test"),
        .service_process = (ProcessPtr) 1,
        .scheduler = (SchedulerPtr) 1,
        .block_count = TEST_FAT32_SECTORS,
    };
    BlockDevice userspace_device = BlockDevice__new(&userspace, block_device.read, block_device.write);
    userspace_device.userspace_service = true;
    userspace.block_device = userspace_device;

    format_volume();
    mount_volume();

    FilesystemPtr fs = Filesystem__heap_new(bstring("fat32"), Fat32FilesystemIO__new());
    Fat32FilesystemIOStatePtr state = Fat32FilesystemIOState__heap_new(fs, &userspace_device, 0);
    state->volume = volume;
    state->volume.device = Some(BlockDevicePtr, &userspace_device);
    state->mounted = true;

    IOInternalHandle handle = 0;
    ResultMatch(IOInternalHandle, fs->file_io.base_io.open(
        &state->state.state, bstring("async.txt"), IO_OPEN_WRITE
    ))
        IsSuccess(opened, handle = opened)
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ByteCount, fs->file_io.base_io.write(&state->state.state, handle, content, sizeof(content)))
        IsSuccess(bytes, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End

    ResultMatch(IOInternalHandle, fs->file_io.base_io.close(&state->state.state, handle))
        IsSuccess(closed, TEST_ASSERT_EQUAL(handle, closed))
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_creates_and_lists_root_directory(void)
{
    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_directory(&volume, bstring("docs")))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries, {
            BString expected = BSTRING_NEW("DOCS");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(BString__equals(&entries.data[0].name, &expected));
            TEST_ASSERT_EQUAL(FAT32_ATTR_DIRECTORY, entries.data[0].attributes);
            TEST_ASSERT_EQUAL(0, entries.data[0].size);
            TEST_ASSERT_TRUE(entries.data[0].first_cluster >= 2);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_rejects_duplicate_root_file(void)
{
    Data content[] = { 'x' };

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, bstring("a.bin"), content, sizeof(content)))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, bstring("A.BIN"), content, sizeof(content)))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(FAT32_ALREADY_EXISTS, code))
    End
}

void test_fat32_rejects_names_outside_8_3_format(void)
{
    Data content[] = { 'x' };

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(
        &volume,
        bstring("too-long-name.txt"),
        content,
        sizeof(content)
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(FAT32_UNSUPPORTED_NAME, code))
    End

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(
        &volume,
        bstring("name!.txt"),
        content,
        sizeof(content)
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(FAT32_UNSUPPORTED_NAME, code))
    End
}

void test_fat32_rejects_file_larger_than_one_cluster_for_now(void)
{
    Data content[FAT32_MAX_FILE_SIZE + 1] = { 0 };

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(
        &volume,
        bstring("large.bin"),
        content,
        sizeof(content)
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(FAT32_FILE_TOO_LARGE, code))
    End
}

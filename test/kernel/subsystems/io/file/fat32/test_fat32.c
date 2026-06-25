#include "unity.h"

#include "../../../../../../kernel/subsystems/io/block/memory_block_device.h"
#include "../../../../../../kernel/subsystems/io/file/fat32/fat32.h"
#include "../../../../stubs/panic.h"

#define TEST_FAT32_SECTORS 256

static MemoryBlockDevice memory_device;
static BlockDevice block_device;
static Fat32Volume volume;

void setUp(void)
{
    clear_panic_state();
    memory_device = MemoryBlockDevice__new();
    block_device = MemoryBlockDevice__as_block_device(&memory_device);
    volume = Fat32Volume__new(&block_device, TEST_FAT32_SECTORS);
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

void test_fat32_formats_and_mounts_volume(void)
{
    format_volume();

    Fat32Volume mounted = Fat32Volume__new(&block_device, 0);
    ResultMatch(Fat32Code, Fat32Volume__mount(&mounted))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(TEST_FAT32_SECTORS, mounted.total_sectors);
    TEST_ASSERT_EQUAL(FAT32_RESERVED_SECTORS, mounted.fat_start_sector);
    TEST_ASSERT_EQUAL(FAT32_ROOT_CLUSTER, mounted.root_cluster);
    TEST_ASSERT_TRUE(mounted.data_start_sector > mounted.fat_start_sector);
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

void test_fat32_creates_lists_and_reads_root_file(void)
{
    Data content[] = { 'h', 'e', 'l', 'l', 'o' };
    Data target[16] = { 0 };
    uint32_t bytes_read = 0;

    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, kstring("hello.txt"), content, sizeof(content)))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries, {
            KString expected = KSTRING_NEW("HELLO.TXT");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(KString__equals(&entries.data[0].name, &expected));
            TEST_ASSERT_EQUAL(FAT32_ATTR_ARCHIVE, entries.data[0].attributes);
            TEST_ASSERT_EQUAL(sizeof(content), entries.data[0].size);
            TEST_ASSERT_TRUE(entries.data[0].first_cluster >= 2);
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32Code, Fat32Volume__read_root_file(
        &volume,
        kstring("HELLO.TXT"),
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

void test_fat32_creates_and_lists_root_directory(void)
{
    format_volume();
    mount_volume();

    ResultMatch(Fat32Code, Fat32Volume__create_root_directory(&volume, kstring("docs")))
        IsSuccess(code, TEST_ASSERT_EQUAL(FAT32_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32DirectoryEntries, Fat32Volume__list_root(&volume))
        IsSuccess(entries, {
            KString expected = KSTRING_NEW("DOCS");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(KString__equals(&entries.data[0].name, &expected));
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

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, kstring("a.bin"), content, sizeof(content)))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(Fat32Code, Fat32Volume__create_root_file(&volume, kstring("A.BIN"), content, sizeof(content)))
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
        kstring("too-long-name.txt"),
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
        kstring("large.bin"),
        content,
        sizeof(content)
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(FAT32_FILE_TOO_LARGE, code))
    End
}

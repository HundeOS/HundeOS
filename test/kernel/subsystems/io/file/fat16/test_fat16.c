#include "unity.h"

#include "../../../../../../kernel/subsystems/io/block/memory_block_device.h"
#include "../../../../../../kernel/subsystems/io/file/fat16/fat16.h"
#include "../../../../stubs/panic.h"

#define TEST_FAT16_SECTORS 256

static MemoryBlockDevice memory_device;
static BlockDevice block_device;
static Fat16Volume volume;

void setUp(void)
{
    clear_panic_state();
    memory_device = MemoryBlockDevice__new();
    block_device = MemoryBlockDevice__as_block_device(&memory_device);
    volume = Fat16Volume__new(&block_device, TEST_FAT16_SECTORS);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

static void format_volume(void)
{
    match_result(Fat16Code, Fat16Volume__format(&volume), success(code) { TEST_ASSERT_EQUAL(FAT16_OK, code); }, error_test_fail());
}

static void mount_volume(void)
{
    match_result(Fat16Code, Fat16Volume__mount(&volume), success(code) { TEST_ASSERT_EQUAL(FAT16_OK, code); }, error_test_fail());
}

void test_fat16_formats_and_mounts_volume(void)
{
    format_volume();

    Fat16Volume mounted = Fat16Volume__new(&block_device, 0);
    match_result(Fat16Code, Fat16Volume__mount(&mounted), success(code) { TEST_ASSERT_EQUAL(FAT16_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(TEST_FAT16_SECTORS, mounted.total_sectors);
    TEST_ASSERT_EQUAL(FAT16_RESERVED_SECTORS, mounted.fat_start_sector);
    TEST_ASSERT_TRUE(mounted.data_start_sector > mounted.root_dir_start_sector);
}

void test_fat16_lists_empty_root_directory_after_format(void)
{
    format_volume();
    mount_volume();

    match_result(Fat16DirectoryEntries, Fat16Volume__list_root(&volume), success(entries) { TEST_ASSERT_EQUAL(0, entries.len); }, error_test_fail());
}

void test_fat16_creates_lists_and_reads_root_file(void)
{
    Data content[] = { 'h', 'e', 'l', 'l', 'o' };
    Data target[16] = { 0 };
    uint32_t bytes_read = 0;

    format_volume();
    mount_volume();

    match_result(Fat16Code, Fat16Volume__create_root_file(&volume, bstring("hello.txt"), content, sizeof(content)), success(code) { TEST_ASSERT_EQUAL(FAT16_OK, code); }, error_test_fail());

    match_result(Fat16DirectoryEntries, Fat16Volume__list_root(&volume), success(entries) {
            BString expected = BSTRING_NEW("HELLO.TXT");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(BString__equals(&entries.data[0].name, &expected));
            TEST_ASSERT_EQUAL(FAT16_ATTR_ARCHIVE, entries.data[0].attributes);
            TEST_ASSERT_EQUAL(sizeof(content), entries.data[0].size);
            TEST_ASSERT_TRUE(entries.data[0].first_cluster >= 2);
        }, error_test_fail());

    match_result(Fat16Code, Fat16Volume__read_root_file(
        &volume,
        bstring("HELLO.TXT"),
        target,
        sizeof(target),
        &bytes_read
    ), success(code) { TEST_ASSERT_EQUAL(FAT16_OK, code); }, error_test_fail());

    TEST_ASSERT_EQUAL(sizeof(content), bytes_read);
    TEST_ASSERT_EQUAL_MEMORY(content, target, sizeof(content));
}

void test_fat16_creates_and_lists_root_directory(void)
{
    format_volume();
    mount_volume();

    match_result(Fat16Code, Fat16Volume__create_root_directory(&volume, bstring("docs")), success(code) { TEST_ASSERT_EQUAL(FAT16_OK, code); }, error_test_fail());

    match_result(Fat16DirectoryEntries, Fat16Volume__list_root(&volume), success(entries) {
            BString expected = BSTRING_NEW("DOCS");
            TEST_ASSERT_EQUAL(1, entries.len);
            TEST_ASSERT_TRUE(BString__equals(&entries.data[0].name, &expected));
            TEST_ASSERT_EQUAL(FAT16_ATTR_DIRECTORY, entries.data[0].attributes);
            TEST_ASSERT_EQUAL(0, entries.data[0].size);
            TEST_ASSERT_TRUE(entries.data[0].first_cluster >= 2);
        }, error_test_fail());
}

void test_fat16_rejects_duplicate_root_file(void)
{
    Data content[] = { 'x' };

    format_volume();
    mount_volume();

    match_result(Fat16Code, Fat16Volume__create_root_file(&volume, bstring("a.bin"), content, sizeof(content)), success_do_nothing(), error_test_fail());

    match_result(Fat16Code, Fat16Volume__create_root_file(&volume, bstring("A.BIN"), content, sizeof(content)), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(FAT16_ALREADY_EXISTS, code); });
}

void test_fat16_rejects_names_outside_8_3_format(void)
{
    Data content[] = { 'x' };

    format_volume();
    mount_volume();

    match_result(Fat16Code, Fat16Volume__create_root_file(
        &volume,
        bstring("too-long-name.txt"),
        content,
        sizeof(content)
    ), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(FAT16_UNSUPPORTED_NAME, code); });

    match_result(Fat16Code, Fat16Volume__create_root_file(
        &volume,
        bstring("name!.txt"),
        content,
        sizeof(content)
    ), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(FAT16_UNSUPPORTED_NAME, code); });
}

void test_fat16_rejects_file_larger_than_one_cluster_for_now(void)
{
    Data content[FAT16_MAX_FILE_SIZE + 1] = { 0 };

    format_volume();
    mount_volume();

    match_result(Fat16Code, Fat16Volume__create_root_file(
        &volume,
        bstring("large.bin"),
        content,
        sizeof(content)
    ), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(FAT16_FILE_TOO_LARGE, code); });
}

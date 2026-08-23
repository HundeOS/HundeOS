#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"

static const char * next_fstab;
static int64_t next_fstab_position;
static IOStatus next_mount_status;
static uint64_t mount_count;
static char mounted_devices[4][96];
static char mounted_filesystems[4][96];
static char mounted_mountpoints[4][96];
static char output[512];
static uint64_t output_position;

#define HUMPEN_ENTRYPOINT humpen_main_under_test
#include "../../../../user/services/humpen/main.c"

void setUp(void)
{
    next_fstab = "";
    next_fstab_position = 0;
    next_mount_status = IO_OK;
    mount_count = 0;
    output_position = 0;
    output[0] = '\0';
    for (uint64_t i = 0; i < 4; i++)
    {
        mounted_devices[i][0] = '\0';
        mounted_filesystems[i][0] = '\0';
        mounted_mountpoints[i][0] = '\0';
    }
}

void tearDown(void)
{
}

IOHandle open(const char * path, Flags flags)
{
    TEST_ASSERT_EQUAL_STRING(HUMPEN_FSTAB_PATH, path);
    TEST_ASSERT_EQUAL(IO_OPEN_READ, flags);
    return 3;
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    TEST_ASSERT_EQUAL(3, handle);

    DynString text;
    match_result(DynStringCode, DynString__init(&text, max_bytes), success(_) {}, error(code, message) { return None(DynString); })

    while (next_fstab[next_fstab_position] != '\0' && next_fstab_position < max_bytes)
    {
        match_result(DynStringCode, DynString__push_char(&text, next_fstab[next_fstab_position]), success(code) {}, error(code, message) {
                DynString__dispose(&text);
                return None(DynString);
            })

        next_fstab_position++;
    }

    return Some(DynString, text);
}

IOStatus close(IOHandle handle)
{
    TEST_ASSERT_EQUAL(3, handle);
    return IO_OK;
}

IOStatus IO__mount_device(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    TEST_ASSERT_LESS_THAN_UINT64(4, mount_count);
    snprintf(mounted_devices[mount_count], sizeof(mounted_devices[mount_count]), "%s", BString__cstr(device_path));
    snprintf(mounted_filesystems[mount_count], sizeof(mounted_filesystems[mount_count]), "%s", BString__cstr(filesystem_type));
    snprintf(mounted_mountpoints[mount_count], sizeof(mounted_mountpoints[mount_count]), "%s", BString__cstr(mountpoint));
    mount_count++;
    return next_mount_status;
}

void print(const char * str)
{
    while (*str != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = *str++;
    }
    output[output_position] = '\0';
}

int printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + output_position, sizeof(output) - output_position, format, args);
    va_end(args);
    if (written > 0)
    {
        output_position += (uint64_t) written;
    }
    return written;
}

void test_humpen_mounts_device_entries_from_fstab(void)
{
    next_fstab =
        "# device <path> <filesystem> <mountpoint>\n"
        "device /dev/virtio1 ext2 /\n"
        "\n"
        "device /dev/virtio3 fat32 /mnt/fs1 # data\n";

    TEST_ASSERT_EQUAL_INT(0, humpen_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(2, mount_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", mounted_devices[0]);
    TEST_ASSERT_EQUAL_STRING("ext2", mounted_filesystems[0]);
    TEST_ASSERT_EQUAL_STRING("/", mounted_mountpoints[0]);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio3", mounted_devices[1]);
    TEST_ASSERT_EQUAL_STRING("fat32", mounted_filesystems[1]);
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1", mounted_mountpoints[1]);
}

void test_humpen_returns_failure_when_mount_fails(void)
{
    next_fstab = "device /dev/virtio3 fat32 /mnt/fs1\n";
    next_mount_status = IO_INVALID_PATH;

    TEST_ASSERT_EQUAL_INT(1, humpen_main_under_test());
    TEST_ASSERT_EQUAL_UINT64(1, mount_count);
    TEST_ASSERT_NOT_NULL(strstr(output, "failed to mount"));
}

void test_humpen_handles_comments_blank_lines_and_missing_final_newline(void)
{
    next_fstab =
        "# device <path> <filesystem> <mountpoint>\n"
        "   \r\n"
        "device /dev/virtio1 ext2 / # root";

    TEST_ASSERT_EQUAL_INT(0, humpen_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(1, mount_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", mounted_devices[0]);
    TEST_ASSERT_EQUAL_STRING("ext2", mounted_filesystems[0]);
    TEST_ASSERT_EQUAL_STRING("/", mounted_mountpoints[0]);
}

void test_humpen_rejects_trailing_tokens_before_mounting(void)
{
    next_fstab = "device /dev/virtio1 ext2 / extra\n";

    TEST_ASSERT_EQUAL_INT(1, humpen_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(0, mount_count);
}

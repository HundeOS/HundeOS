#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/spawn.h"

static const char * next_fstab;
static int64_t next_fstab_position;
static SpawnResult next_spawn_result;
static uint64_t spawn_count;
static char spawned_paths[4][96];
static char spawned_arg0[4][96];
static char spawned_arg1[4][96];
static char spawned_arg2[4][96];
static char output[512];
static uint64_t output_position;

#define HUMPEN_ENTRYPOINT humpen_main_under_test
#include "../../../../user/services/humpen/main.c"

void setUp(void)
{
    next_fstab = "";
    next_fstab_position = 0;
    next_spawn_result = 42;
    spawn_count = 0;
    output_position = 0;
    output[0] = '\0';
    for (uint64_t i = 0; i < 4; i++)
    {
        spawned_paths[i][0] = '\0';
        spawned_arg0[i][0] = '\0';
        spawned_arg1[i][0] = '\0';
        spawned_arg2[i][0] = '\0';
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
    match_result(DynStringCode, DynString__init(&text, max_bytes), success_do_nothing(), error(code, message) { return None(DynString); });

    while (next_fstab[next_fstab_position] != '\0' && next_fstab_position < max_bytes)
    {
        match_result(DynStringCode, DynString__push_char(&text, next_fstab[next_fstab_position]), success_do_nothing(), error(code, message) {
                DynString__dispose(&text);
                return None(DynString);
            });

        next_fstab_position++;
    }

    return Some(DynString, text);
}

IOStatus close(IOHandle handle)
{
    TEST_ASSERT_EQUAL(3, handle);
    return IO_OK;
}

SpawnResult spawn(SpawnMode mode, const char * path, const char * arg0, const char * arg1, const char * arg2)
{
    TEST_ASSERT_EQUAL(P_NOWAIT, mode);
    TEST_ASSERT_LESS_THAN_UINT64(4, spawn_count);
    snprintf(spawned_paths[spawn_count], sizeof(spawned_paths[spawn_count]), "%s", path);
    snprintf(spawned_arg0[spawn_count], sizeof(spawned_arg0[spawn_count]), "%s", arg0);
    snprintf(spawned_arg1[spawn_count], sizeof(spawned_arg1[spawn_count]), "%s", arg1);
    snprintf(spawned_arg2[spawn_count], sizeof(spawned_arg2[spawn_count]), "%s", arg2);
    spawn_count++;
    return next_spawn_result;
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

void test_humpen_starts_watchdogs_for_device_entries_from_fstab(void)
{
    next_fstab =
        "# device <path> <filesystem> <mountpoint>\n"
        "device /dev/virtio1 ext2 /\n"
        "\n"
        "device /dev/virtio3 fat32 /mnt/fs1 # data\n";

    TEST_ASSERT_EQUAL_INT(0, humpen_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(2, spawn_count);
    TEST_ASSERT_EQUAL_STRING(HUMPEN_WACHHUND_PATH, spawned_paths[0]);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", spawned_arg0[0]);
    TEST_ASSERT_EQUAL_STRING("ext2", spawned_arg1[0]);
    TEST_ASSERT_EQUAL_STRING("/", spawned_arg2[0]);
    TEST_ASSERT_EQUAL_STRING(HUMPEN_WACHHUND_PATH, spawned_paths[1]);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio3", spawned_arg0[1]);
    TEST_ASSERT_EQUAL_STRING("fat32", spawned_arg1[1]);
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1", spawned_arg2[1]);
}

void test_humpen_returns_failure_when_watchdog_spawn_fails(void)
{
    next_fstab = "device /dev/virtio3 fat32 /mnt/fs1\n";
    next_spawn_result = SPAWN_FAILED;

    TEST_ASSERT_EQUAL_INT(1, humpen_main_under_test());
    TEST_ASSERT_EQUAL_UINT64(1, spawn_count);
    TEST_ASSERT_NOT_NULL(strstr(output, "failed to start wachhund"));
}

void test_humpen_handles_comments_blank_lines_and_missing_final_newline(void)
{
    next_fstab =
        "# device <path> <filesystem> <mountpoint>\n"
        "   \r\n"
        "device /dev/virtio1 ext2 / # root";

    TEST_ASSERT_EQUAL_INT(0, humpen_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(1, spawn_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", spawned_arg0[0]);
    TEST_ASSERT_EQUAL_STRING("ext2", spawned_arg1[0]);
    TEST_ASSERT_EQUAL_STRING("/", spawned_arg2[0]);
}

void test_humpen_rejects_trailing_tokens_before_mounting(void)
{
    next_fstab = "device /dev/virtio1 ext2 / extra\n";

    TEST_ASSERT_EQUAL_INT(1, humpen_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(0, spawn_count);
}

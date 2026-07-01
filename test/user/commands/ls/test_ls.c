#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"

static DirectoryEntry fake_entries[4];
static DirectoryEntryCount fake_count;
static DirectoryEntryCount fake_result;
static char requested_path[512];
static char output[512];
static uint64_t output_position;

#define LS_ENTRYPOINT ls_main_under_test
#include "../../../../user/commands/ls/main.c"
#include "../../../../user/sys/io.c"

void setUp(void)
{
    fake_count = 0;
    fake_result = 0;
    output_position = 0;
    output[0] = '\0';
    requested_path[0] = '\0';
    for (uint64_t i = 0; i < 4; i++)
    {
        fake_entries[i] = (DirectoryEntry) { 0 };
    }
}

void tearDown(void)
{
}

static void copy_text(char * target, const char * source, uint64_t target_size)
{
    uint64_t position = 0;
    while (position + 1 < target_size && source[position] != '\0')
    {
        target[position] = source[position];
        position++;
    }
    target[position] = '\0';
}

Anything syscall(SystemCall call, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    TEST_ASSERT_EQUAL(IO_CALL, call);
    TEST_ASSERT_EQUAL(IO_LIST_DIR, arg1);
    copy_text(requested_path, (const char *) arg2, sizeof(requested_path));

    if (fake_result < 0)
    {
        return fake_result;
    }

    DirectoryEntryPtr entries = (DirectoryEntryPtr) arg3;
    uint64_t max_entries = (uint64_t) arg4;
    uint64_t count = fake_count;
    if (count > max_entries)
    {
        count = max_entries;
    }

    for (uint64_t i = 0; i < count; i++)
    {
        entries[i] = fake_entries[i];
    }

    return (Anything) count;
}

void print(const char * str)
{
    uint64_t i = 0;
    while (str[i] != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = str[i++];
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

void test_ls_prints_entries_for_directory(void)
{
    fake_count = 2;
    copy_text(fake_entries[0].name, "one", IO_DIRENT_NAME_MAX);
    copy_text(fake_entries[1].name, "two", IO_DIRENT_NAME_MAX);

    char * argv[] = { "ls", "/mnt" };

    TEST_ASSERT_EQUAL_INT(0, ls_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_STRING("/mnt", requested_path);
    TEST_ASSERT_EQUAL_STRING("one\ntwo\n", output);
}

void test_ls_prints_named_io_error(void)
{
    fake_result = IO_INVALID_PATH;
    char * argv[] = { "ls", "/mnt/fs1" };

    TEST_ASSERT_EQUAL_INT(1, ls_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1", requested_path);
    TEST_ASSERT_EQUAL_STRING("ls: error IO_INVALID_PATH (-9)\n", output);
}

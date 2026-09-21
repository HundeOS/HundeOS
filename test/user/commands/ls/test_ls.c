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
#include "../../../../user/lib/sys/io.c"

void setUp(void)
{
    fake_count = 0;
    fake_result = 0;
    output_position = 0;
    output[0] = '\0';
    requested_path[0] = '\0';
    for (uint64_t i = 0; i < 4; i++)
    {
        fake_entries[i] = (DirectoryEntry){0};
    }
}

void tearDown(void)
{
}

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    TEST_ASSERT_EQUAL(IO_REQUEST_LIST_DIR, operation);
    BString__copy_to(path, requested_path, sizeof(requested_path));
    IOReply reply = IOReply__new(fake_result < 0 ? fake_result : IO_OK, offset < fake_count ? 1 : 0);
    if (offset < fake_count)
    {
        reply.entry = fake_entries[offset];
    }
    return reply;
}

void IO__print(ConstBStringPtr message)
{
    const char * str = BString__cstr(message);
    uint64_t i = 0;
    while (str[i] != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = str[i++];
    }
    output[output_position] = '\0';
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString formatted = BString__vsprintf(format, args);
    va_end(args);
    StringPos written = BString__copy_to(&formatted, output + output_position, sizeof(output) - output_position);
    if (written > 0)
    {
        output_position += (uint64_t)written;
    }
    return (int)written;
}

void test_ls_prints_entries_for_directory(void)
{
    fake_count = 2;
    BString one = bstr("one");
    fake_entries[0] = DirectoryEntry__new(0, 0, 0, 0, &one);
    fake_entries[1] = DirectoryEntry__new(0, 0, 0, 0, &bstr("two"));

    char * argv[] = {"ls", "/mnt"};

    TEST_ASSERT_EQUAL_INT(0, ls_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_STRING("/mnt", requested_path);
    TEST_ASSERT_EQUAL_STRING("one\ntwo\n", output);
}

void test_ls_prints_named_io_error(void)
{
    fake_result = IO_INVALID_PATH;
    char * argv[] = {"ls", "/mnt/fs1"};

    TEST_ASSERT_EQUAL_INT(1, ls_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1", requested_path);
    TEST_ASSERT_EQUAL_STRING("ls: error IO_INVALID_PATH (-9)\n", output);
}

void test_ls_long_format_prints_mode_owner_group_and_name(void)
{
    fake_count = 1;
    BString drivers = bstr("drivers");
    fake_entries[0] = DirectoryEntry__new(
        0, PROCESS_USER_HUND, PROCESS_GROUP_HUND,
        IO_MODE_TYPE_DIR | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC, &drivers);

    char * argv[] = {"ls", "-l", "/sbin"};

    TEST_ASSERT_EQUAL_INT(0, ls_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_STRING("/sbin", requested_path);
    TEST_ASSERT_EQUAL_STRING("dr-xr-x--- hund hund drivers\n", output);
}

void test_ls_a_hides_dot_entries_unless_requested(void)
{
    fake_count = 2;
    BString hidden = bstr(".hidden");
    BString shown = bstr("shown");
    fake_entries[0] = DirectoryEntry__new(0, 0, 0, 0, &hidden);
    fake_entries[1] = DirectoryEntry__new(0, 0, 0, 0, &shown);

    char * argv[] = {"ls", "/"};
    TEST_ASSERT_EQUAL_INT(0, ls_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_STRING("shown\n", output);

    output_position = 0;
    output[0] = '\0';

    char * all_argv[] = {"ls", "-a", "/"};
    TEST_ASSERT_EQUAL_INT(0, ls_main_under_test(3, all_argv));
    TEST_ASSERT_EQUAL_STRING(".hidden\nshown\n", output);
}

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    if (!BString__starts_with(path, &bstr(IO_SOCKET_SCHEME)))
    {
        return IO_PATH_UNMANAGED;
    }
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem link");
    return IO_OPERATION_NOT_SUPPORTED;
}

IOHandle IOClient__accept(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

IOStatus IOClient__close(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return IO_UNKNOWN_HANDLE;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    TEST_FAIL_MESSAGE("Unexpected socket IO");
    return -1;
}

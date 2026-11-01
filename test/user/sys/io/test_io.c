#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/calls.h"
#include "../../../../common_includes/syscalls/io.h"

#include "../../../../user/lib/io/client.h"
static IORequestOperation last_operation;
static IOHandle last_handle;
static IOPathRequest last_path_request;
static IOLinkRequest last_link_request;
static ConstChannelDataPtr last_write_data;
static size_t last_write_size;
static const char * next_read_data;
static ByteCount next_read_result;
static int64_t next_result;
static IOReply next_path_reply;

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    last_operation = IO_REQUEST_OPEN;
    last_path_request = (IOPathRequest){.path = *path, .flags = flags};
    return next_result;
}

Result(IOPipe) IOClient__pipe(void)
{
    TEST_FAIL_MESSAGE("Unexpected pipe");
    return Error(IOPipe, bstr("unexpected pipe"), IO_OPERATION_NOT_SUPPORTED);
}

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    last_operation = IO_REQUEST_LINK;
    last_link_request = (IOLinkRequest){.source_path = *source_path, .target_path = *target_path};
    return next_result;
}

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    last_operation = operation;
    last_path_request = (IOPathRequest){.path = *path, .flags = flags};
    return operation == IO_REQUEST_FILE_INFO ? next_path_reply : IOReply__new(next_result, 0);
}

IOHandle IOClient__accept(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected accept");
    return IO_UNKNOWN_HANDLE;
}

IOStatus IOClient__close(IOHandle handle)
{
    return IO_OK;
}

IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid)
{
    return IO_OPERATION_NOT_SUPPORTED;
}

IOTerminalAttributesResult IOClient__get_terminal_attributes(IOHandle handle)
{
    return IOTerminalAttributesResult__new(IO_OPERATION_NOT_SUPPORTED, (IOTerminalAttributes){0});
}

IOStatus IOClient__set_terminal_attributes(IOHandle handle, IOTerminalAttributes attributes)
{
    return IO_OPERATION_NOT_SUPPORTED;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    last_operation = IO_REQUEST_READ;
    last_handle = handle;
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(size, next_read_result);
    if (next_read_result > 0)
    {
        memcpy(data, next_read_data, next_read_result);
    }
    return next_read_result;
}

ByteCount IOClient__read_with_flags(IOHandle handle, ChannelDataPtr data, size_t size, Flags flags)
{
    return IOClient__read(handle, data, size);
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    last_operation = IO_REQUEST_WRITE;
    last_handle = handle;
    last_write_data = data;
    last_write_size = size;
    return next_result;
}

ByteCount IOClient__write_with_flags(IOHandle handle, ConstChannelDataPtr data, size_t size, Flags flags)
{
    return IOClient__write(handle, data, size);
}

IOReply IOClient__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

IOReply IOClient__watch_file(IOHandle handle)
{
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

IOReply IOClient__wait_file_change(IOFileWatch * watches, size_t count)
{
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

#include "../../../../user/lib/sys/io.c"

void setUp(void)
{
    last_operation = 0;
    last_handle = 0;
    last_path_request = (IOPathRequest){0};
    last_link_request = (IOLinkRequest){0};
    next_read_data = NULL;
    next_read_result = 0;
    next_result = 0;
    next_path_reply = IOReply__new(IO_OK, 0);
    IO__set_startup_stdio(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
}

void tearDown(void)
{
}

void test_io_open_uses_bstring_path(void)
{
    BString path = bstring("/mnt/fs1/test.txt");
    next_result = 42;

    match_result(
        IO__open(&path, IO_OPEN_READ),
        success(handle)
        {
            TEST_ASSERT_EQUAL_INT64(42, handle);
            TEST_ASSERT_EQUAL(IO_REQUEST_OPEN, last_operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fs1/test.txt"), last_path_request.path);
            TEST_ASSERT_EQUAL(IO_OPEN_READ, last_path_request.flags);
        },
        error_test_fail()
    );
}

void test_io_path_from_arg_normalizes_root_relative_components(void)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/"), IO__path_from_arg(&bstr(".")));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/usr/local"), IO__path_from_arg(&bstr("./usr//share/../local/")));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/etc"), IO__path_from_arg(&bstr("/../../etc")));
}

void test_io_read_populates_bstring_and_sets_length(void)
{
    BString buffer = bstring("stale");
    next_read_data = "abc";
    next_read_result = 3;

    match_result(
        IO__read(IO_SESSION_HANDLE_BASE + 9, &buffer),
        success(read_count)
        {
            TEST_ASSERT_EQUAL_INT64(3, read_count);
            TEST_ASSERT_EQUAL(IO_REQUEST_READ, last_operation);
            TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 9, last_handle);
            TEST_ASSERT_EQUAL_BSTRING(bstr("abc"), buffer);
        },
        error_test_fail()
    );
}

void test_io_read_dyn_string_returns_file_contents(void)
{
    next_read_data = "abc";
    next_read_result = 3;

    match_result(
        IO__read_dyn_string(IO_SESSION_HANDLE_BASE + 9, 16),
        success(text)
        {
            TEST_ASSERT_EQUAL(IO_REQUEST_READ, last_operation);
            TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 9, last_handle);
            TEST_ASSERT_EQUAL_STRING("abc", DynString__cstr(&text));
            TEST_ASSERT_EQUAL_INT64(3, DynString__length(&text));
            DynString__dispose(&text);
        },
        error_test_fail()
    );
}

void test_io_write_uses_bstring_length(void)
{
    next_result = 5;

    match_result(
        IO__write(IO_SESSION_HANDLE_BASE + 7, &bstr("hello")),
        success(written)
        {
            TEST_ASSERT_EQUAL_INT64(5, written);
            TEST_ASSERT_EQUAL(IO_REQUEST_WRITE, last_operation);
            TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 7, last_handle);
            TEST_ASSERT_EQUAL_STRING("hello", (const char *)last_write_data);
            TEST_ASSERT_EQUAL_INT64(5, last_write_size);
        },
        error_test_fail()
    );
}

void test_io_make_dir_uses_bstring_path(void)
{
    BString path = bstring("/mnt/fs1/newdir");
    next_result = IO_OK;

    match_result(
        IO__make_dir(&path),
        success(status)
        {
            TEST_ASSERT_EQUAL(IO_OK, status);
            TEST_ASSERT_EQUAL(IO_REQUEST_MKDIR, last_operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/fs1/newdir"), last_path_request.path);
        },
        error_test_fail()
    );
}

void test_io_link_uses_bstring_paths(void)
{
    BString source_path = bstring("/mnt/ext2fs/hello.txt");
    BString target_path = bstring("/mnt/ext2fs/copy.txt");
    next_result = IO_OK;

    match_result(
        IO__link_file(&source_path, &target_path),
        success(status)
        {
            TEST_ASSERT_EQUAL(IO_OK, status);
            TEST_ASSERT_EQUAL(IO_REQUEST_LINK, last_operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/ext2fs/hello.txt"), last_link_request.source_path);
            TEST_ASSERT_EQUAL_BSTRING(bstr("/mnt/ext2fs/copy.txt"), last_link_request.target_path);
        },
        error_test_fail()
    );
}

void test_io_file_info_uses_one_path_request(void)
{
    next_path_reply.file_info = (IOFileInfo){
        .mode = IO_MODE_TYPE_FILE | 0644, .user_id = PROCESS_USER_USER, .group_id = PROCESS_GROUP_USER, .size = 123};

    match_result(
        IO__file_info(&bstr("/tmp/file")),
        success(info)
        {
            TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | 0644, info.mode);
            TEST_ASSERT_EQUAL(PROCESS_USER_USER, info.user_id);
            TEST_ASSERT_EQUAL(PROCESS_GROUP_USER, info.group_id);
            TEST_ASSERT_EQUAL(123, info.size);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(IO_REQUEST_FILE_INFO, last_operation);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/tmp/file"), last_path_request.path);
}

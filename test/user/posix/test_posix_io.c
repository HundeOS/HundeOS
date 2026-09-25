#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"

#include "../../../common/ipc/io_protocol.h"
#include "../../../user/lib/sys/io.h"

static IOHandle opened_handle;
static BString opened_path;
static Flags opened_flags;
static IOHandle closed_handle;
static ByteCount next_read_result;
static ByteCount next_write_result;

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    opened_path = *path;
    opened_flags = flags;
    return opened_handle;
}

BString IO__path_from_arg(ConstBStringPtr argument)
{
    if (BString__starts_with(argument, &bstr("/")))
    {
        return *argument;
    }
    return BString__append(&bstr("/"), argument);
}

ByteCount IO__read_bytes(IOHandle handle, char * buffer, size_t size)
{
    if (next_read_result > 0)
    {
        memcpy(buffer, "data", (size_t)next_read_result);
    }
    return next_read_result;
}

ByteCount IO__write_bytes(IOHandle handle, const char * buffer, size_t size)
{
    return next_write_result;
}

IOStatus IO__close(IOHandle handle)
{
    closed_handle = handle;
    return IO_OK;
}

IOStatus IO__remove_file(ConstBStringPtr path)
{
    return IO_INVALID_PATH;
}

#include "../../../user/lib/newlib/posix_io.c"

void setUp(void)
{
    opened_handle = IO_SESSION_HANDLE_BASE + 19;
    opened_path = empty_bstr();
    opened_flags = 0;
    closed_handle = 0;
    next_read_result = IO_EOF;
    next_write_result = 0;
    errno = 0;
}

void tearDown(void)
{
}

void test_posix_open_maps_native_handle_to_small_descriptor(void)
{
    int descriptor = open("etc/config", O_RDONLY);

    TEST_ASSERT_EQUAL_INT(3, descriptor);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/etc/config"), opened_path);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, opened_flags);
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 19, closed_handle);
}

void test_posix_read_converts_native_eof_to_zero(void)
{
    int descriptor = open("file", O_RDONLY);

    char buffer[4];
    TEST_ASSERT_EQUAL_INT64(0, read(descriptor, buffer, sizeof(buffer)));
    close(descriptor);
}

void test_posix_read_converts_native_failure_to_errno(void)
{
    int descriptor = open("file", O_RDONLY);
    next_read_result = IO_PERMISSION_DENIED;

    char buffer[4];
    TEST_ASSERT_EQUAL_INT64(-1, read(descriptor, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
    close(descriptor);
}

void test_posix_read_maps_interrupted_stream_to_eintr(void)
{
    int descriptor = open("file", O_RDONLY);
    next_read_result = IO_INTERRUPTED;

    char buffer[4];
    TEST_ASSERT_EQUAL_INT64(-1, read(descriptor, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(EINTR, errno);
    close(descriptor);
}

void test_posix_close_recycles_descriptor(void)
{
    int first = open("first", O_RDONLY);
    TEST_ASSERT_EQUAL_INT(0, close(first));

    opened_handle++;
    int second = open("second", O_RDONLY);
    TEST_ASSERT_EQUAL_INT(first, second);
    close(second);
}

void test_posix_fsync_accepts_open_descriptor_and_rejects_unknown_descriptor(void)
{
    int descriptor = open("file", O_WRONLY);

    TEST_ASSERT_EQUAL_INT(0, fsync(descriptor));
    TEST_ASSERT_EQUAL_INT(-1, fsync(descriptor + 1));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
    close(descriptor);
}

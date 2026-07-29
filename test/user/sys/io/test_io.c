#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/calls.h"
#include "../../../../common_includes/syscalls/io.h"

static SystemCall last_call;
static Anything last_arg1;
static Anything last_arg2;
static Anything last_arg3;
static Anything last_arg4;
static Anything last_arg5;
static const char * next_read_data;
static ByteCount next_read_result;
static SystemCallResult next_result;

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    last_call = num;
    last_arg1 = arg1;
    last_arg2 = arg2;
    last_arg3 = arg3;
    last_arg4 = arg4;
    last_arg5 = arg5;

    if (num == IO_CALL && arg1 == IO_READ && next_read_result > 0)
    {
        memcpy((void *) arg3, next_read_data, (size_t) next_read_result);
        return next_read_result;
    }

    return next_result;
}

#include "../../../../user/sys/io.c"

void setUp(void)
{
    last_call = 0;
    last_arg1 = 0;
    last_arg2 = 0;
    last_arg3 = 0;
    last_arg4 = 0;
    last_arg5 = 0;
    next_read_data = NULL;
    next_read_result = 0;
    next_result = 0;
}

void tearDown(void)
{
}

void test_io_open_uses_bstring_path(void)
{
    BString path = bstring("/mnt/fs1/test.txt");
    next_result = 42;

    IOHandle handle = IO__open(&path, IO_OPEN_READ);

    TEST_ASSERT_EQUAL_INT64(42, handle);
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_OPEN, last_arg1);
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1/test.txt", (const char *) last_arg2);
    TEST_ASSERT_EQUAL(IO_OPEN_READ, last_arg3);
}

void test_io_read_populates_bstring_and_sets_length(void)
{
    BString buffer = bstring("stale");
    next_read_data = "abc";
    next_read_result = 3;

    ByteCount read_count = IO__read(9, &buffer);

    TEST_ASSERT_EQUAL_INT64(3, read_count);
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_READ, last_arg1);
    TEST_ASSERT_EQUAL_INT64(9, last_arg2);
    TEST_ASSERT_EQUAL_STRING("abc", buffer.data);
    TEST_ASSERT_EQUAL_INT64(3, buffer.len);
}

void test_io_write_uses_bstring_length(void)
{
    BString value = bstring("hello");
    next_result = 5;

    ByteCount written = IO__write(7, &value);

    TEST_ASSERT_EQUAL_INT64(5, written);
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_WRITE, last_arg1);
    TEST_ASSERT_EQUAL_INT64(7, last_arg2);
    TEST_ASSERT_EQUAL_STRING("hello", (const char *) last_arg3);
    TEST_ASSERT_EQUAL_INT64(5, last_arg4);
}

void test_io_make_dir_uses_bstring_path(void)
{
    BString path = bstring("/mnt/fs1/newdir");
    next_result = IO_OK;

    IOStatus status = IO__make_dir(&path);

    TEST_ASSERT_EQUAL(IO_OK, status);
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_MKDIR, last_arg1);
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1/newdir", (const char *) last_arg2);
}

void test_io_link_uses_bstring_paths(void)
{
    BString source_path = bstring("/mnt/ext2fs/hello.txt");
    BString target_path = bstring("/mnt/ext2fs/copy.txt");
    next_result = IO_OK;

    IOStatus status = IO__link_file(&source_path, &target_path);

    TEST_ASSERT_EQUAL(IO_OK, status);
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_LINK, last_arg1);
    TEST_ASSERT_EQUAL_STRING("/mnt/ext2fs/hello.txt", (const char *) last_arg2);
    TEST_ASSERT_EQUAL_STRING("/mnt/ext2fs/copy.txt", (const char *) last_arg3);
}

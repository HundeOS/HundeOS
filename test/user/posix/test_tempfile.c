#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "unity.h"

static unsigned open_calls;
static unsigned mkdir_calls;
static unsigned lstat_calls;
static char first_candidate[64];
static char second_candidate[64];
static int collision_count;
static int operation_error;
static int open_flags;
static mode_t creation_mode;

int HundTest__open(const char * path, int flags, ...)
{
    va_list arguments;
    va_start(arguments, flags);
    creation_mode = (mode_t)va_arg(arguments, int);
    va_end(arguments);

    open_flags = flags;
    open_calls++;
    strcpy(open_calls == 1 ? first_candidate : second_candidate, path);
    if ((int)open_calls <= collision_count)
    {
        errno = EEXIST;
        return -1;
    }
    if (operation_error != 0)
    {
        errno = operation_error;
        return -1;
    }
    return 7;
}

int HundTest__mkdir(const char * path, mode_t mode)
{
    creation_mode = mode;
    mkdir_calls++;
    strcpy(mkdir_calls == 1 ? first_candidate : second_candidate, path);
    if ((int)mkdir_calls <= collision_count)
    {
        errno = EEXIST;
        return -1;
    }
    if (operation_error != 0)
    {
        errno = operation_error;
        return -1;
    }
    return 0;
}

int HundTest__lstat(const char * path, struct stat * status)
{
    lstat_calls++;
    strcpy(lstat_calls == 1 ? first_candidate : second_candidate, path);
    if ((int)lstat_calls <= collision_count)
    {
        return 0;
    }
    errno = operation_error == 0 ? ENOENT : operation_error;
    return -1;
}

pid_t HundTest__getpid(void)
{
    return 42;
}

int HundTest__rand(void)
{
    return 1234;
}

#define getpid HundTest__getpid
#define lstat HundTest__lstat
#define mkdir HundTest__mkdir
#define open HundTest__open
#define rand HundTest__rand
#include "../../../user/lib/newlib/tempfile.c"
#undef getpid
#undef lstat
#undef mkdir
#undef open
#undef rand

void setUp(void)
{
    open_calls = 0;
    mkdir_calls = 0;
    lstat_calls = 0;
    first_candidate[0] = '\0';
    second_candidate[0] = '\0';
    collision_count = 0;
    operation_error = 0;
    open_flags = 0;
    creation_mode = 0;
    tempfile_sequence = 0;
    tempfile_sequence_initialized = false;
    errno = 0;
}

void tearDown(void)
{
}

void test_mkstemp_retries_collisions_with_atomic_exclusive_creation(void)
{
    char path[] = "/tmp/bash.XXXXXX";
    collision_count = 1;

    TEST_ASSERT_EQUAL_INT(7, mkstemp(path));
    TEST_ASSERT_EQUAL_UINT(2, open_calls);
    TEST_ASSERT_EQUAL_INT(O_RDWR | O_CREAT | O_EXCL, open_flags);
    TEST_ASSERT_EQUAL_INT(0600, creation_mode);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first_candidate, second_candidate));
    TEST_ASSERT_EQUAL_STRING(second_candidate, path);
}

void test_mkdtemp_retries_collisions_with_private_directory_creation(void)
{
    char path[] = "/tmp/bash.XXXXXX";
    collision_count = 1;

    TEST_ASSERT_EQUAL_PTR(path, mkdtemp(path));
    TEST_ASSERT_EQUAL_UINT(2, mkdir_calls);
    TEST_ASSERT_EQUAL_INT(0700, creation_mode);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first_candidate, second_candidate));
    TEST_ASSERT_EQUAL_STRING(second_candidate, path);
}

void test_mktemp_returns_an_unused_name_after_a_collision(void)
{
    char path[] = "/tmp/bash.XXXXXX";
    collision_count = 1;

    TEST_ASSERT_EQUAL_PTR(path, mktemp(path));
    TEST_ASSERT_EQUAL_UINT(2, lstat_calls);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first_candidate, second_candidate));
    TEST_ASSERT_EQUAL_STRING(second_candidate, path);
}

void test_tempfile_functions_reject_a_template_without_six_trailing_xs(void)
{
    char file_path[] = "/tmp/file.XXXXX";
    char directory_path[] = "/tmp/dir.XXXXX";
    char name_path[] = "/tmp/name.XXXXX";

    TEST_ASSERT_EQUAL_INT(-1, mkstemp(file_path));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_NULL(mkdtemp(directory_path));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_PTR(name_path, mktemp(name_path));
    TEST_ASSERT_EQUAL_STRING("", name_path);
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_UINT(0, open_calls + mkdir_calls + lstat_calls);
}

void test_tempfile_functions_preserve_noncollision_errors(void)
{
    char file_path[] = "/tmp/file.XXXXXX";
    char directory_path[] = "/tmp/dir.XXXXXX";
    char name_path[] = "/tmp/name.XXXXXX";
    operation_error = EACCES;

    TEST_ASSERT_EQUAL_INT(-1, mkstemp(file_path));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
    TEST_ASSERT_NULL(mkdtemp(directory_path));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
    TEST_ASSERT_EQUAL_PTR(name_path, mktemp(name_path));
    TEST_ASSERT_EQUAL_STRING("", name_path);
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
}

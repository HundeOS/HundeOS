#include "unity.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys/io.h"

static IOStatus file_info_status;
static FileMode file_mode;
static BString requested_path;

BString IO__path_join(ConstBStringPtr base, ConstBStringPtr child)
{
    BString prefix = BString__append(base, &bstr("/"));
    return BString__append(&prefix, child);
}

BString IO__path_from_arg(ConstBStringPtr path)
{
    if (BString__equals(path, &bstr("/work/project/../shared")))
    {
        return bstr("/work/shared");
    }
    if (BString__equals(path, &bstr("/work/project/src/../include")))
    {
        return bstr("/work/project/include");
    }
    return *path;
}

Result(IOFileInfo) IO__file_info(ConstBStringPtr path)
{
    requested_path = *path;
    return file_info_status == IO_OK
               ? Success(IOFileInfo, IOFileInfo__new(file_mode, PROCESS_USER_HUND, PROCESS_GROUP_HUND, 0))
               : Error(IOFileInfo, bstr("unavailable"), file_info_status);
}

#include "../../../user/lib/newlib/current_directory.c"

void setUp(void)
{
    LibC__set_current_directory("/");
    file_info_status = IO_OK;
    file_mode = IO_MODE_TYPE_DIR | 0755;
    requested_path = empty_bstr();
    errno = 0;
}

void tearDown(void)
{
}

void test_relative_paths_are_resolved_from_the_current_directory(void)
{
    BString absolute;
    LibC__set_current_directory("/work/project");

    TEST_ASSERT_EQUAL_INT(0, PosixPath__absolute("src/../include", &absolute));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/work/project/include"), absolute);
}

void test_chdir_updates_getcwd_after_verifying_a_directory(void)
{
    char buffer[64];
    LibC__set_current_directory("/work/project");

    TEST_ASSERT_EQUAL_INT(0, chdir("../shared"));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/work/shared"), requested_path);
    TEST_ASSERT_EQUAL_PTR(buffer, getcwd(buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("/work/shared", buffer);
}

void test_chdir_rejects_a_non_directory_without_changing_cwd(void)
{
    char buffer[64];
    LibC__set_current_directory("/work/project");
    file_mode = IO_MODE_TYPE_FILE | 0644;

    TEST_ASSERT_EQUAL_INT(-1, chdir("../shared"));
    TEST_ASSERT_EQUAL_INT(ENOTDIR, errno);
    TEST_ASSERT_EQUAL_PTR(buffer, getcwd(buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("/work/project", buffer);
}

void test_getcwd_reports_a_small_buffer(void)
{
    char buffer[4];
    LibC__set_current_directory("/work");

    TEST_ASSERT_NULL(getcwd(buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(ERANGE, errno);
}

void test_getcwd_allocates_an_exact_buffer_when_requested(void)
{
    LibC__set_current_directory("/work/project");

    char * buffer = getcwd(NULL, 0);

    TEST_ASSERT_NOT_NULL(buffer);
    TEST_ASSERT_EQUAL_STRING("/work/project", buffer);
    free(buffer);
}

void test_getcwd_rejects_a_missing_fixed_size_buffer(void)
{
    TEST_ASSERT_NULL(getcwd(NULL, 16));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}

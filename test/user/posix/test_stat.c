#include <errno.h>
#include <sys/stat.h>

#include "unity.h"

#include "sys/io.h"

static unsigned file_info_requests;
static IOStatus file_info_status;
static IOFileInfo file_info;
static IOStatus make_dir_status;

Result(IOFileInfo) IO__file_info(ConstBStringPtr path)
{
    file_info_requests++;
    TEST_ASSERT_EQUAL_BSTRING(bstr("/usr/local/bin/tool"), *path);
    return file_info_status == IO_OK ? Success(IOFileInfo, file_info)
                                     : Error(IOFileInfo, bstr("missing"), file_info_status);
}

int PosixPath__absolute(const char * path, BStringPtr result)
{
    *result = BString__new(path);
    return 0;
}

IOStatus IO__make_dir(ConstBStringPtr path)
{
    return make_dir_status;
}

IOHandle PosixIO__native_handle(int descriptor)
{
    return IO_UNKNOWN_HANDLE;
}

Optional(BString) PosixIO__path(int descriptor)
{
    return None(BString);
}

#define fchflags HundTest__fchflags
#include "../../../user/lib/newlib/stat.c"
#undef fchflags

void setUp(void)
{
    file_info_requests = 0;
    file_info_status = IO_OK;
    file_info = (IOFileInfo){
        .mode = IO_MODE_TYPE_FILE | 0755, .user_id = PROCESS_USER_HUND, .group_id = PROCESS_GROUP_HUND, .size = 1234};
    make_dir_status = IO_OPERATION_NOT_SUPPORTED;
}

void tearDown(void)
{
}

void test_stat_uses_single_file_information_request(void)
{
    struct stat status;

    TEST_ASSERT_EQUAL(0, stat("/usr/local/bin/tool", &status));
    TEST_ASSERT_EQUAL(1, file_info_requests);
    TEST_ASSERT_EQUAL(S_IFREG | 0755, status.st_mode);
    TEST_ASSERT_EQUAL(PROCESS_USER_HUND, status.st_uid);
    TEST_ASSERT_EQUAL(PROCESS_GROUP_HUND, status.st_gid);
    TEST_ASSERT_EQUAL(1234, status.st_size);
}

void test_stat_maps_missing_path_without_directory_scan(void)
{
    struct stat status;
    file_info_status = IO_INVALID_PATH;

    TEST_ASSERT_EQUAL(-1, stat("/usr/local/bin/tool", &status));
    TEST_ASSERT_EQUAL(ENOENT, errno);
    TEST_ASSERT_EQUAL(1, file_info_requests);
}

void test_stat_exposes_bsd_sticky_bit_name(void)
{
    TEST_ASSERT_EQUAL(S_ISTXT, S_ISVTX);
}

void test_mkdir_reports_an_existing_path_as_eexist(void)
{
    make_dir_status = IO_ALREADY_EXISTS;

    TEST_ASSERT_EQUAL_INT(-1, mkdir("/tmp/existing", 0700));
    TEST_ASSERT_EQUAL_INT(EEXIST, errno);
}

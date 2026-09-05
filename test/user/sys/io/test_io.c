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
static IOPathRequest last_path_request;
static IOListDirRequest last_list_dir_request;
static IOLinkRequest last_link_request;
static IOMountRequest last_mount_request;
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

    if (num == IO_CALL)
    {
        if (arg1 == IO_OPEN || arg1 == IO_MKDIR || arg1 == IO_REMOVE)
        {
            last_path_request = *((IOPathRequestPtr) arg2);
        }
        if (arg1 == IO_LIST_DIR)
        {
            last_list_dir_request = *((IOListDirRequestPtr) arg2);
        }
        if (arg1 == IO_LINK)
        {
            last_link_request = *((IOLinkRequestPtr) arg2);
        }
        if (arg1 == IO_MOUNT)
        {
            last_mount_request = *((IOMountRequestPtr) arg2);
        }
    }

    if (num == IO_CALL && arg1 == IO_READ && next_read_result > 0)
    {
        memcpy((void *) arg3, next_read_data, (size_t) next_read_result);
        return next_read_result;
    }

    return next_result;
}

#include "../../../../user/lib/sys/io.c"
#include "../../../../user/lib/sys/sd_host_config.h"

static DynString dyn_string_from_cstr(const char * text)
{
    DynString string;
    match_result(DynStringCode, DynString__init(&string, 128), success_do_nothing(), error_test_fail());

    match_result(DynStringCode, DynString__append_cstr(&string, text), success_do_nothing(), error_test_fail());

    return string;
}

void setUp(void)
{
    last_call = 0;
    last_arg1 = 0;
    last_arg2 = 0;
    last_arg3 = 0;
    last_arg4 = 0;
    last_arg5 = 0;
    last_path_request = (IOPathRequest) { 0 };
    last_list_dir_request = (IOListDirRequest) { 0 };
    last_link_request = (IOLinkRequest) { 0 };
    last_mount_request = (IOMountRequest) { 0 };
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
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1/test.txt", BString__cstr(&last_path_request.path));
    TEST_ASSERT_EQUAL(IO_OPEN_READ, last_path_request.flags);
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

void test_io_read_dyn_string_returns_file_contents(void)
{
    next_read_data = "abc";
    next_read_result = 3;

    match_optional(DynString, IO__read_dyn_string(9, 16), some(text) {
            TEST_ASSERT_EQUAL(IO_CALL, last_call);
            TEST_ASSERT_EQUAL(IO_READ, last_arg1);
            TEST_ASSERT_EQUAL_INT64(9, last_arg2);
            TEST_ASSERT_EQUAL_STRING("abc", DynString__cstr(&text));
            TEST_ASSERT_EQUAL_INT64(3, DynString__length(&text));
            DynString__dispose(&text);
        }, none_test_fail());
}

void test_io_write_uses_bstring_length(void)
{
    next_result = 5;

    ByteCount written = IO__write(7, &bstr("hello"));

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
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1/newdir", BString__cstr(&last_path_request.path));
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
    TEST_ASSERT_EQUAL_STRING("/mnt/ext2fs/hello.txt", BString__cstr(&last_link_request.source_path));
    TEST_ASSERT_EQUAL_STRING("/mnt/ext2fs/copy.txt", BString__cstr(&last_link_request.target_path));
}

void test_io_mount_uses_bstring_paths(void)
{
    BString device_path = bstring("/dev/virtio3");
    BString filesystem_type = bstring("fat32");
    BString mountpoint = bstring("/mnt/fs1");
    next_result = IO_OK;

    IOStatus status = IO__mount_device(&device_path, &filesystem_type, &mountpoint);

    TEST_ASSERT_EQUAL(IO_OK, status);
    TEST_ASSERT_EQUAL(IO_CALL, last_call);
    TEST_ASSERT_EQUAL(IO_MOUNT, last_arg1);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio3", BString__cstr(&last_mount_request.device_path));
    TEST_ASSERT_EQUAL_STRING("fat32", BString__cstr(&last_mount_request.filesystem_type));
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1", BString__cstr(&last_mount_request.mountpoint));
}

void test_sd_host_config_parse_reads_present_host(void)
{
    DynString sysinfo = dyn_string_from_cstr(
        "machine=qemu_virt\n"
        "sd_host.present=1\n"
        "sd_host.physical_base=0xfe340000\n"
        "sd_host.mmio_size=0x100\n"
        "sd_host.irq=126\n"
    );

    match_optional(SdHostConfig, SdHostConfig__parse(&sysinfo), some(config) {
            TEST_ASSERT_EQUAL_HEX64(0xfe340000, config.physical_base);
            TEST_ASSERT_EQUAL_HEX64(0x100, config.mmio_size);
            TEST_ASSERT_EQUAL_UINT64(126, config.irq);
        }, none_test_fail());

    DynString__dispose(&sysinfo);
}

void test_sd_host_config_parse_returns_none_when_host_absent(void)
{
    DynString sysinfo = dyn_string_from_cstr(
        "sd_host.present=0\n"
        "sd_host.physical_base=0xfe340000\n"
        "sd_host.mmio_size=0x100\n"
        "sd_host.irq=126\n"
    );

    match_optional(SdHostConfig, SdHostConfig__parse(&sysinfo), some_test_fail(), none_do_nothing());

    DynString__dispose(&sysinfo);
}

void test_sd_host_config_parse_rejects_partial_numeric_values(void)
{
    DynString sysinfo = dyn_string_from_cstr(
        "sd_host.present=1\n"
        "sd_host.physical_base=0xfe340000tail\n"
        "sd_host.mmio_size=0x100\n"
        "sd_host.irq=126\n"
    );

    match_optional(SdHostConfig, SdHostConfig__parse(&sysinfo), some_test_fail(), none_do_nothing());

    DynString__dispose(&sysinfo);
}

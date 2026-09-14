#include "unity.h"
#include "io/client.h"
#include "syscalls/calls.h"
#include "sys/syscall.h"

static unsigned kernel_calls;
static unsigned channel_calls;
static IOReply replies[3];
static IOHandle opened;

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    channel_calls++;
    TEST_ASSERT_LESS_THAN_UINT64(3, offset);
    return replies[offset];
}
IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    channel_calls++;
    return opened;
}
IOHandle IOClient__accept(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Not a socket test");
    return IO_UNKNOWN_HANDLE;
}
IOStatus IOClient__close(IOHandle handle)
{
    channel_calls++;
    return IO_OK;
}
ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    channel_calls++;
    return 0;
}
ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    channel_calls++;
    return size;
}

SystemCallResult syscall(SystemCall call, Anything operation, Anything arg2, Anything arg3, Anything arg4,
                         Anything arg5)
{
    kernel_calls++;
    if (operation == IO_LIST_DIR)
    {
        IOListDirRequestPtr request = (IOListDirRequestPtr)arg2;
        request->entries[0] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_DIR, &bstr("mnt"));
        return 1;
    }
    return operation == IO_OPEN ? 7 : IO_OK;
}

#include "../../../user/lib/sys/io.c"

void setUp(void)
{
    kernel_calls = 0;
    channel_calls = 0;
    opened = IO_FILESYSTEM_HANDLE_BASE;
    replies[0] = IOReply__new(IO_OK, 0);
    replies[1] = IOReply__new(IO_OK, 0);
    replies[2] = IOReply__new(IO_OK, 0);
}
void tearDown(void)
{
}

void test_filesystem_handles_use_channel_for_read_write_close(void)
{
    IOHandle handle = open(&bstr("/mnt/ext/file"), IO_OPEN_READ | IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, handle);
    char data[2] = "x";
    TEST_ASSERT_EQUAL(0, read(handle, data, 1));
    TEST_ASSERT_EQUAL(1, write(handle, data, 1));
    TEST_ASSERT_EQUAL(IO_OK, close(handle));
    TEST_ASSERT_EQUAL(4, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_backend_failure_never_falls_back_to_kernel(void)
{
    opened = IO_NO_DEVICE_FOUND;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, open(&bstr("/mnt/ext/file"), IO_OPEN_READ));
    replies[0] = IOReply__new(IO_NO_DEVICE_FOUND, 0);
    DirectoryEntry entry;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, list_dir(&bstr("/mnt/ext"), &entry, 1));
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, make_dir(&bstr("/mnt/ext/new")));
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, remove_file(&bstr("/mnt/ext/file")));
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_only_explicit_unmanaged_reply_uses_kernel_file_path(void)
{
    opened = IO_PATH_UNMANAGED;
    TEST_ASSERT_EQUAL(7, open(&bstr("/root-file"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(1, kernel_calls);
    TEST_ASSERT_EQUAL(1, channel_calls);
}

void test_bootstrap_files_and_terminal_streams_do_not_require_io_service(void)
{
    TEST_ASSERT_EQUAL(7, open(&bstr("/boot/config/fstab"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(7, open(&bstr("/boot/sysinfo"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(7, open(&bstr(":tty:"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(7, open(&bstr(":console:"), IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL(0, channel_calls);
    TEST_ASSERT_EQUAL(4, kernel_calls);
}

void test_embedded_files_use_userspace_channels(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/sbin/login"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(1, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_retired_proc_and_other_boot_paths_do_not_bypass_userspace(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/proc/sysinfo"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/boot/other"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/boot/sysinfo/child"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_tmp_and_former_memfs_paths_use_channels(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/tmp/test"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/tmp/memfs/test"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_devfs_uses_channels_and_only_kernel_stream_reply_falls_back(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, open(&bstr("/dev/hd0"), IO_OPEN_READ));
    DirectoryEntry entry;
    TEST_ASSERT_EQUAL(0, list_dir(&bstr("/dev"), &entry, 1));
    TEST_ASSERT_EQUAL(0, kernel_calls);
    opened = IO_INVALID_PATH;
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, open(&bstr("/dev/missing"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(0, kernel_calls);
    opened = IO_PATH_UNMANAGED;
    TEST_ASSERT_EQUAL(7, open(&bstr("/dev/businfo"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(1, kernel_calls);
}

void test_mount_overlay_replaces_existing_parent_entry_without_duplicate(void)
{
    replies[0] = IOReply__new(IO_PATH_UNMANAGED, 1);
    replies[0].entry = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_DIR | 0755, &bstr("mnt"));
    replies[1] = IOReply__new(IO_PATH_UNMANAGED, 0);
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(1, list_dir(&bstr("/"), entries, 3));
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, entries[0].mode);
    TEST_ASSERT_EQUAL(1, kernel_calls);
}

void test_directory_entries_are_paged_over_channel(void)
{
    replies[0] = IOReply__new(IO_OK, 1);
    replies[0].entry = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("first"));
    replies[1] = IOReply__new(IO_OK, 1);
    replies[1].entry = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("second"));
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(2, list_dir(&bstr("/mnt/ext"), entries, 3));
    TEST_ASSERT_EQUAL_STRING("first", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("second", entries[1].name);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_root_overlay_wins_over_disk_entry_without_repeating_a_page(void)
{
    replies[0] = IOReply__new(IO_OK, 1);
    replies[0].entry = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_DIR | 0755, &bstr("sbin"));
    replies[1] = IOReply__new(IO_OK, 1);
    replies[1].entry = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("sbin"));
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(1, list_dir(&bstr("/"), entries, 3));
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, entries[0].mode);
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

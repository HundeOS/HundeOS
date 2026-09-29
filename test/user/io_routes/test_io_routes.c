#include "unity.h"
#include "io/client.h"
#include "syscalls/calls.h"
#include "sys/syscall.h"

static unsigned kernel_calls;
static unsigned channel_calls;
static IOReply replies[3];
static IOHandle opened;
static uint64_t foreground_pid;

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

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    channel_calls++;
    TEST_ASSERT_EQUAL_BSTRING(bstr("/source"), *source_path);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/target"), *target_path);
    return replies[0].status;
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

IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid)
{
    channel_calls++;
    foreground_pid = pid;
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

IOReply IOClient__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    channel_calls++;
    return replies[0];
}

IOReply IOClient__watch_file(IOHandle handle)
{
    channel_calls++;
    return replies[0];
}

IOReply IOClient__wait_file_change(IOFileWatch * watches, size_t count)
{
    channel_calls++;
    return replies[0];
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
    IO__set_startup_stdio(0, 0, 0);
    kernel_calls = 0;
    channel_calls = 0;
    opened = IO_FILESYSTEM_HANDLE_BASE;
    foreground_pid = 0;
    replies[0] = IOReply__new(IO_OK, 0);
    replies[1] = IOReply__new(IO_OK, 0);
    replies[2] = IOReply__new(IO_OK, 0);
}

void test_terminal_foreground_control_uses_open_standard_input(void)
{
    IO__set_startup_stdio(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    opened = IO_STREAM_HANDLE_BASE;

    TEST_ASSERT_EQUAL(IO_OK, IO__set_terminal_foreground(42));

    TEST_ASSERT_EQUAL_UINT64(42, foreground_pid);
    TEST_ASSERT_EQUAL_UINT(2, channel_calls);
}

void tearDown(void)
{
}

void test_filesystem_handles_use_channel_for_read_write_close(void)
{
    IOHandle handle = IO__open(&bstr("/mnt/ext/file"), IO_OPEN_READ | IO_OPEN_WRITE);
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, handle);
    char data[2] = "x";
    TEST_ASSERT_EQUAL(0, IO__read_bytes(handle, data, 1));
    TEST_ASSERT_EQUAL(1, IO__write_bytes(handle, data, 1));
    TEST_ASSERT_EQUAL(IO_OK, IO__close(handle));
    TEST_ASSERT_EQUAL(4, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_backend_failure_never_falls_back_to_kernel(void)
{
    opened = IO_NO_DEVICE_FOUND;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, IO__open(&bstr("/mnt/ext/file"), IO_OPEN_READ));
    replies[0] = IOReply__new(IO_NO_DEVICE_FOUND, 0);
    DirectoryEntry entry;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, list_dir(&bstr("/mnt/ext"), &entry, 1));
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, make_dir(&bstr("/mnt/ext/new")));
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, remove_file(&bstr("/mnt/ext/file")));
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_unmanaged_reply_never_uses_kernel_file_path(void)
{
    opened = IO_PATH_UNMANAGED;
    TEST_ASSERT_EQUAL(IO_PATH_UNMANAGED, IO__open(&bstr("/root-file"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(0, kernel_calls);
    TEST_ASSERT_EQUAL(1, channel_calls);
}

void test_boot_files_use_channels_without_kernel_syscalls(void)
{
    TEST_ASSERT_EQUAL(opened, IO__open(&bstr("/boot/config/fstab"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(opened, IO__open(&bstr("/boot/sysinfo"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_terminal_paths_use_channels_without_kernel_fallback(void)
{
    opened = IO_STREAM_HANDLE_BASE;
    TEST_ASSERT_EQUAL(opened, IO__open(&bstr(":tty:"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(opened, IO__open(&bstr(":console:"), IO_OPEN_WRITE));
    opened = IO_NO_DEVICE_FOUND;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, IO__open(&bstr(":console:"), IO_OPEN_WRITE));
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_link_uses_channel_and_preserves_errors(void)
{
    replies[0] = IOReply__new(IO_OK, 0);
    TEST_ASSERT_EQUAL(IO_OK, link_file(&bstr("/source"), &bstr("/target")));
    replies[0].status = IO_OPERATION_NOT_SUPPORTED;
    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, link_file(&bstr("/source"), &bstr("/target")));
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_legacy_handles_never_fall_back_to_syscalls(void)
{
    char data[8] = {0};
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, IO__read_bytes(7, data, sizeof(data)));
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, IO__write_bytes(7, data, sizeof(data)));
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, IO__close(7));
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, accept(7));
    TEST_ASSERT_EQUAL(0, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_zero_capacity_listing_does_not_send_a_request(void)
{
    DirectoryEntry entry;
    TEST_ASSERT_EQUAL(0, list_dir(&bstr("/"), &entry, 0));
    TEST_ASSERT_EQUAL(0, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_terminal_stdio_opens_once_and_close_does_not_reopen(void)
{
    IO__set_startup_stdio(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    opened = IO_STREAM_HANDLE_BASE;
    char data = 'x';
    TEST_ASSERT_EQUAL(1, IO__write_bytes(1, &data, 1));
    TEST_ASSERT_EQUAL(1, IO__write_bytes(1, &data, 1));
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(IO_OK, IO__close(1));
    TEST_ASSERT_EQUAL(IO_NOT_OPEN, IO__write_bytes(1, &data, 1));
    TEST_ASSERT_EQUAL(4, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_keyboard_input_uses_channels_without_kernel_fallback(void)
{
    opened = IO_STREAM_HANDLE_BASE;
    TEST_ASSERT_EQUAL(opened, IO__open(&bstr(":keyboard:"), IO_OPEN_READ));
    IO__set_startup_stdio(IO_ENDPOINT_KEYBOARD_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    char byte;
    TEST_ASSERT_EQUAL(0, IO__read_bytes(0, &byte, 1));
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
    opened = IO_NO_DEVICE_FOUND;
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, IO__open(&bstr(":keyboard:"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_stdio_open_failure_can_retry_without_using_kernel_console(void)
{
    IO__set_startup_stdio(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    opened = IO_NO_DEVICE_FOUND;
    char data = 'x';
    TEST_ASSERT_EQUAL(IO_NO_DEVICE_FOUND, IO__write_bytes(1, &data, 1));
    opened = IO_STREAM_HANDLE_BASE;
    TEST_ASSERT_EQUAL(1, IO__write_bytes(1, &data, 1));
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_embedded_files_use_userspace_channels(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/sbin/login"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(1, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_retired_proc_and_other_boot_paths_do_not_bypass_userspace(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/proc/sysinfo"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/boot/other"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/boot/sysinfo/child"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(3, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_tmp_and_former_memfs_paths_use_channels(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/tmp/test"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/tmp/memfs/test"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_devfs_uses_channels_without_kernel_stream_fallback(void)
{
    TEST_ASSERT_EQUAL(IO_FILESYSTEM_HANDLE_BASE, IO__open(&bstr("/dev/hd0"), IO_OPEN_READ));
    DirectoryEntry entry;
    TEST_ASSERT_EQUAL(0, list_dir(&bstr("/dev"), &entry, 1));
    TEST_ASSERT_EQUAL(0, kernel_calls);
    opened = IO_INVALID_PATH;
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, IO__open(&bstr("/dev/missing"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(0, kernel_calls);
    opened = IO_PATH_UNMANAGED;
    TEST_ASSERT_EQUAL(IO_PATH_UNMANAGED, IO__open(&bstr("/dev/test-stream"), IO_OPEN_READ));
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_unmanaged_directory_reply_is_not_merged_with_kernel_entries(void)
{
    replies[0] = IOReply__new(IO_PATH_UNMANAGED, 1);
    replies[0].directory.entries[0] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_DIR | 0755, &bstr("mnt"));
    replies[1] = IOReply__new(IO_PATH_UNMANAGED, 0);
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(IO_PATH_UNMANAGED, list_dir(&bstr("/"), entries, 3));
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_directory_entries_are_paged_over_channel(void)
{
    replies[0] = IOReply__new(IO_OK, 0);
    replies[0].directory.entries[0] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("first"));
    replies[0].directory.entries[1] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("second"));
    replies[0].directory.count = 2;
    replies[2] = IOReply__new(IO_OK, 0);
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(2, list_dir(&bstr("/mnt/ext"), entries, 3));
    TEST_ASSERT_EQUAL_STRING("first", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("second", entries[1].name);
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_root_overlay_wins_over_disk_entry_without_repeating_a_page(void)
{
    replies[0] = IOReply__new(IO_OK, 0);
    replies[0].directory.entries[0] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_DIR | 0755, &bstr("sbin"));
    replies[0].directory.entries[1] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_FILE, &bstr("sbin"));
    replies[0].directory.count = 2;
    replies[2] = IOReply__new(IO_OK, 0);
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(1, list_dir(&bstr("/"), entries, 3));
    TEST_ASSERT_EQUAL(IO_MODE_TYPE_DIR | 0755, entries[0].mode);
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

void test_synthetic_directory_entries_survive_a_missing_backing_directory(void)
{
    replies[0] = IOReply__new(IO_OK, 0);
    replies[0].directory.entries[0] = DirectoryEntry__new(0, 0, 0, IO_MODE_TYPE_DIR | 0755, &bstr("sd"));
    replies[0].directory.count = 1;
    replies[1] = IOReply__new(IO_INVALID_PATH, 0);
    DirectoryEntry entries[3];
    TEST_ASSERT_EQUAL(1, list_dir(&bstr("/mnt"), entries, 3));
    TEST_ASSERT_EQUAL_STRING("sd", entries[0].name);
    TEST_ASSERT_EQUAL(2, channel_calls);
    TEST_ASSERT_EQUAL(0, kernel_calls);
}

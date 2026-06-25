#include "unity.h"

#include "../../../../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../../../../kernel/syscall/io_handler.h"
#include "../../../../../../kernel/process.h"
#include "../../../../../../kernel/print.h"
#include "../../../../../../kernel/scheduler/scheduler.h"
#include "../../../../../../kernel/subsystems/io/file/filesystem.h"
#include "../../../../../../kernel/subsystems/io/file/fat16/fat16.h"
#include "../../../../../../kernel/subsystems/io/file/fat32/fat32.h"
#include "../../../../../../kernel/subsystems/io/file/memfs/memfs.h"
#include "../../../../fixtures/io_subsystem.h"
#include "../../../../fixtures/process.h"
#include "../../../../fixtures/kernel.h"

static FilesystemPtr root = NULL;
static IOStatePtr root_state = NULL;

static KernelPtr kernel = NULL;
static ProcessPtr dummy_proc = NULL;

static KernelContext kernel_context = EMPTY_KERNEL_CONTEXT;

void setUp(void)
{
    dummy_proc = dummy_process(100, 11, PROC_STATE_RUNNING);

    root = init_root_filesystem_with_embedded_binaries();
    FilesystemPtr fat16_filesystem = Filesystem__heap_new(kstring("fat16"), Fat16FilesystemIO__new());
    Fat16FilesystemIOStatePtr fat16_state = Fat16FilesystemIOState__heap_new(fat16_filesystem, 0);
    (void) fat16_state;
    Filesystem__mount(root, kstring("fat16"), fat16_filesystem);

    FilesystemPtr fat32_filesystem = Filesystem__heap_new(kstring("fat32"), Fat32FilesystemIO__new());
    Fat32FilesystemIOStatePtr fat32_state = Fat32FilesystemIOState__heap_new(fat32_filesystem, 0);
    (void) fat32_state;
    Filesystem__mount(root, kstring("fat32"), fat32_filesystem);

    kernel = init_test_kernel_with_filesystem(root);
    root_state = &root->state->state;

    kernel_context = KernelContext__new(Some(ProcessID, dummy_proc->pid), Some(SchedulerPtr, kernel->scheduler));
}

void tearDown(void)
{

}

static KString embedded = KSTRING_NEW("ebin");
static KString tmp = KSTRING_NEW("tmp");
static KString memfs = KSTRING_NEW("memfs");
static KString fat16 = KSTRING_NEW("fat16");
static KString fat32 = KSTRING_NEW("fat32");
static KString hello_world = KSTRING_NEW("hello_world");
static KString corrupt = KSTRING_NEW("corrupt");

static const uint64_t HELLO_WORLD_SIZE = 66840;

IOContextPtr io_context(Optional(IOHandle) handle, Optional(ExternalBuffer) ext_buffer)
{
    return IOContext__heap_new(kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), handle, ext_buffer);
}

IOContextPtr open_context()
{
    return io_context(None(IOHandle), None(ExternalBuffer));
}

IOContextPtr close_context(IOHandle handle)
{
    return io_context(Some(IOHandle, handle), None(ExternalBuffer));
}

IOContextPtr rw_context(IOHandle handle, DataPtr data, size_t size)
{
    ExternalBuffer ext_buffer = ExternalBuffer__new(data, size);
    return io_context(Some(IOHandle, handle), Some(ExternalBuffer, ext_buffer));
}

static KString numbered_memfs_path(int index)
{
    char path[] = "/tmp/memfs/file00";
    path[15] = '0' + (index / 10);
    path[16] = '0' + (index % 10);
    return kstring(path);
}


void test_it_can_list_files_from_root(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, kstring("/"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(4, FilesystemNodes__len(&result));
            FilesystemNodePtr node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 0));
            TEST_ASSERT_TRUE(KString__equals(&embedded, &node->name));
            FilesystemNodePtr tmp_node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 1));
            TEST_ASSERT_TRUE(KString__equals(&tmp, &tmp_node->name));
            FilesystemNodePtr fat16_node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 2));
            TEST_ASSERT_TRUE(KString__equals(&fat16, &fat16_node->name));
            FilesystemNodePtr fat32_node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 3));
            TEST_ASSERT_TRUE(KString__equals(&fat32, &fat32_node->name));
        });
    End
}

void test_it_can_list_files_to_subdir(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, kstring("/ebin/"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(3, FilesystemNodes__len(&result));
            FilesystemNodePtr node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 0));
            TEST_ASSERT_TRUE(KString__equals(&hello_world, &node->name));
        });
    End
}

void test_it_can_list_nested_mount_parent(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, kstring("/tmp"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(1, FilesystemNodes__len(&result));
            FilesystemNodePtr node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 0));
            TEST_ASSERT_TRUE(KString__equals(&memfs, &node->name));
        });
    End
}

void test_it_can_open_files_and_read_from_embedded_via_io_subsystem(void)
{
    Result(IOHandle) handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/ebin/hello_world"), 0);

    ResultMatch(IOHandle, handle)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(hand, {
            Char buffer[_128KB];

            Result(ByteCount) res =
                IOSubsystem__read(kernel->io_subsystem, rw_context(hand, buffer, sizeof(buffer)));

            ResultMatch(ByteCount, res)
                IsError(code, message,
                {
                    TEST_FAIL();
                })
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL(HELLO_WORLD_SIZE, bytes);
                })
            End
        });
    End
}

void test_io_list_dir_lists_root_mounts(void)
{
    DirectoryEntry entries[5];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/", (Anything) entries, 5, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(4, count);
            TEST_ASSERT_EQUAL_STRING("ebin", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("tmp", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("fat16", entries[2].name);
            TEST_ASSERT_EQUAL_STRING("fat32", entries[3].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_ebin_files(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/ebin", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(3, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_tmp_mounts(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("memfs", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_empty_memfs(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_empty_fat16(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat16", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_empty_fat32(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat16_write_read_and_list_root_file(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat16/NOTE.TXT"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "hello fat16";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 11)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(11, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat16", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("NOTE.TXT", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat16/NOTE.TXT"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(read_hand,
        {
            char buffer[32];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, 11)))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(11, bytes);
                    buffer[11] = '\0';
                    TEST_ASSERT_EQUAL_STRING("hello fat16", buffer);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_fat16_mkdir_lists_root_directory_and_empty_directory(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/", (Anything) ((DirectoryEntry[5]) { 0 }), 5, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(4, count))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/fat16/dir", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry root_entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat16", (Anything) root_entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("DIR", root_entries[0].name);
            TEST_ASSERT_EQUAL_INT(FAT16_ATTR_DIRECTORY, root_entries[0].attributes);
        })
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry dir_entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat16/dir", (Anything) dir_entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_write_read_and_list_root_file(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/NOTE.TXT"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "hello fat32";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 11)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(11, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("NOTE.TXT", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/NOTE.TXT"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(read_hand,
        {
            char buffer[32];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, 11)))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(11, bytes);
                    buffer[11] = '\0';
                    TEST_ASSERT_EQUAL_STRING("hello fat32", buffer);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_fat32_mkdir_lists_root_directory_and_empty_directory(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/fat32/dir", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry root_entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32", (Anything) root_entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("DIR", root_entries[0].name);
            TEST_ASSERT_EQUAL_INT(FAT32_ATTR_DIRECTORY, root_entries[0].attributes);
        })
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry dir_entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32/dir", (Anything) dir_entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_nested_open_missing_parent_fails_cleanly_repeatedly(void)
{
    Result(IOHandle) first =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/bin/file.txt"), IO_OPEN_WRITE);
    ResultMatch(IOHandle, first)
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End

    Result(IOHandle) second =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/bin/file.txt"), IO_OPEN_WRITE);
    ResultMatch(IOHandle, second)
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_fat32_write_read_and_list_nested_file(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/fat32/bin", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/bin/file.txt"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "nested fat32";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 12)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(12, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32/bin", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("FILE.TXT", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/bin/file.txt"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(read_hand,
        {
            char buffer[32];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, 12)))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(12, bytes);
                    buffer[12] = '\0';
                    TEST_ASSERT_EQUAL_STRING("nested fat32", buffer);
                })
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, 12)))
                IsSuccess(bytes, TEST_FAIL())
                IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_EOF, code))
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(read_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(read_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_fat32_nested_file_read_returns_eof_through_syscall_handler(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/fat32/dir", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_OPEN, (Anything) "/fat32/dir/file.txt", IO_OPEN_WRITE, 0, 0
    ))
        IsSuccess(fd,
        {
            char data[] = "hello";
            ResultMatch(SystemCallResult, IO__handler(
                kernel, Some(ProcessPtr, dummy_proc), IO_WRITE, fd, (Anything) data, 5, 0
            ))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(5, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(SystemCallResult, IO__handler(
                kernel, Some(ProcessPtr, dummy_proc), IO_CLOSE, fd, 0, 0, 0
            ))
                IsSuccess(closed_fd, TEST_ASSERT_EQUAL_INT(fd, closed_fd))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_OPEN, (Anything) "/fat32/dir/file.txt", IO_OPEN_READ, 0, 0
    ))
        IsSuccess(fd,
        {
            char buffer[513];
            ResultMatch(SystemCallResult, IO__handler(
                kernel, Some(ProcessPtr, dummy_proc), IO_READ, fd, (Anything) buffer, 512, 0
            ))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(5, bytes);
                    buffer[5] = '\0';
                    TEST_ASSERT_EQUAL_STRING("hello", buffer);
                })
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(SystemCallResult, IO__handler(
                kernel, Some(ProcessPtr, dummy_proc), IO_READ, fd, (Anything) buffer, 512, 0
            ))
                IsSuccess(bytes, TEST_FAIL())
                IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_EOF, code))
            End

            ResultMatch(SystemCallResult, IO__handler(
                kernel, Some(ProcessPtr, dummy_proc), IO_CLOSE, fd, 0, 0, 0
            ))
                IsSuccess(closed_fd, TEST_ASSERT_EQUAL_INT(fd, closed_fd))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_memfs_write_read_and_list_file(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/notes"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "hello memfs";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 11)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(11, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("notes", entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/notes"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(read_hand,
        {
            char buffer[32];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, 11)))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(11, bytes);
                    buffer[11] = '\0';
                    TEST_ASSERT_EQUAL_STRING("hello memfs", buffer);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_memfs_mkdir_lists_directory_and_allows_file_inside(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/tmp/memfs/docs", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry root_entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs", (Anything) root_entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("docs", root_entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/docs/notes"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsSuccess(write_hand,
        {
            char data[] = "nested";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data) - 1)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry docs_entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs/docs", (Anything) docs_entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("notes", docs_entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_fat32_rm_removes_nested_file(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/fat32/dir", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/dir/file.txt"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "hi";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 2)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(2, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/fat32/dir/file.txt", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32/dir", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/dir/file.txt"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_fat32_rm_recursive_removes_directory_tree(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/fat32/dir", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/dir/file.txt"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "hi";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 2)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(2, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/fat32/dir", IO_REMOVE_RECURSIVE, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/fat32", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/fat32/dir/file.txt"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_memfs_rm_removes_file_from_directory(void)
{
    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/remove-me"), IO_OPEN_WRITE))
        IsSuccess(write_hand,
        {
            char data[] = "delete";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data) - 1)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/tmp/memfs/remove-me", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/remove-me"), IO_OPEN_READ))
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_memfs_rm_directory_returns_unsupported(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/tmp/memfs/docs", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/tmp/memfs/docs", 0, 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End
}

void test_memfs_rm_recursive_removes_directory_tree(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/tmp/memfs/docs", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/tmp/memfs/docs/nested", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/docs/nested/notes"), IO_OPEN_WRITE))
        IsSuccess(write_hand,
        {
            char data[] = "nested";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data) - 1)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/tmp/memfs/docs", IO_REMOVE_RECURSIVE, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    DirectoryEntry entries[4];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(0, count))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/docs/nested/notes"), IO_OPEN_READ))
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_mkdir_and_rm_on_embeddedfs_return_unsupported(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/ebin/newdir", 0, 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/ebin/hello_world", 0, 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End
}

void test_memfs_file_can_grow_beyond_one_page(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/big"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[PAGE_SIZE + 17];
            for (size_t i = 0; i < sizeof(data); i++)
            {
                data[i] = 'a' + (i % 26);
            }

            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data))))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data), bytes))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/big"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(read_hand,
        {
            char buffer[PAGE_SIZE + 17];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, sizeof(buffer))))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(sizeof(buffer), bytes);
                    for (size_t i = 0; i < sizeof(buffer); i++)
                    {
                        TEST_ASSERT_EQUAL_CHAR('a' + (i % 26), buffer[i]);
                    }
                })
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_memfs_read_returns_error_when_file_pages_are_missing(void)
{
    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/corrupt"), IO_OPEN_WRITE))
        IsSuccess(write_hand,
        {
            char data[] = "data";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data) - 1)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(closed, TEST_ASSERT_EQUAL(write_hand, closed))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    Match(FilesystemPtr, Mounts__get(&root->mounts, kstring("tmp/memfs")))
        IsNone(TEST_FAIL())
        IsSome(mounted_fs,
        {
            MemFilesystemIOStatePtr mem_state = (MemFilesystemIOStatePtr) mounted_fs->state;
            LLIterate(MemFiles, mem_state->files, file)
                if (KString__equals(&file.name, &corrupt))
                {
                    file__node->value.pages = MemFilePages__new();
                    file__node->value.capacity = PAGE_SIZE;
                    file__node->value.size = sizeof("data") - 1;
                }
            End
        })
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/corrupt"), IO_OPEN_READ))
        IsSuccess(read_hand,
        {
            char buffer[8];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, sizeof(buffer))))
                IsSuccess(bytes, TEST_FAIL())
                IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
            End
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_memfs_write_open_truncates_existing_file(void)
{
    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/truncate"), IO_OPEN_WRITE))
        IsSuccess(write_hand,
        {
            char data[] = "longer value";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data) - 1)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(closed, TEST_ASSERT_EQUAL(write_hand, closed))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/truncate"), IO_OPEN_WRITE))
        IsSuccess(write_hand,
        {
            char data[] = "short";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, sizeof(data) - 1)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(closed, TEST_ASSERT_EQUAL(write_hand, closed))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), kstring("/tmp/memfs/truncate"), IO_OPEN_READ))
        IsSuccess(read_hand,
        {
            char buffer[16];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr) buffer, sizeof(buffer))))
                IsSuccess(bytes,
                {
                    TEST_ASSERT_EQUAL_INT(5, bytes);
                    buffer[5] = '\0';
                    TEST_ASSERT_EQUAL_STRING("short", buffer);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_memfs_can_create_more_than_sixty_four_files(void)
{
    for (int i = 0; i < 65; i++)
    {
        KString path = numbered_memfs_path(i);
        ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), path, IO_OPEN_WRITE))
            IsSuccess(handle,
            {
                ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(handle)))
                    IsSuccess(closed, TEST_ASSERT_EQUAL(handle, closed))
                    IsError(code, message, TEST_FAIL())
                End
            })
            IsError(code, message, TEST_FAIL())
        End
    }

    DirectoryEntry entries[70];
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp/memfs", (Anything) entries, 70, 0
    ))
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(65, count))
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_invalid_path_returns_error(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/missing", (Anything) entries, 4, 0
    ))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

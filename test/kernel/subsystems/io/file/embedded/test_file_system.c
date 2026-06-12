#include "unity.h"

#include "../../../../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../../../../kernel/syscall/io_handler.h"
#include "../../../../../../kernel/process.h"
#include "../../../../../../kernel/print.h"
#include "../../../../../../kernel/scheduler/scheduler.h"
#include "../../../../../../kernel/subsystems/io/file/filesystem.h"
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
    kernel = init_test_kernel_with_filesystem(root);
    root_state = &root->state->state;

    kernel_context = KernelContext__new(Some(ProcessID, dummy_proc->pid), Some(SchedulerPtr, kernel->scheduler));
}

void tearDown(void)
{

}

static KString embedded = KSTRING_NEW("ebin");
static KString hello_world = KSTRING_NEW("hello_world");

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


void test_it_can_list_files_from_root(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, kstring("/"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(1, FilesystemNodes__len(&result));
            FilesystemNodePtr node = UnwrapPanic(FilesystemNodePtr, FilesystemNodes__at(&result, 0));
            TEST_ASSERT_TRUE(KString__equals(&embedded, &node->name));
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
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/", (Anything) entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("ebin", entries[0].name);
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

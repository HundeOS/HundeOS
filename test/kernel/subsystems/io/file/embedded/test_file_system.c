#include "unity.h"

#include "../../../../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../../../../kernel/process.h"
#include "../../../../../../kernel/scheduler/scheduler.h"
#include "../../../../../../kernel/subsystems/io/file/filesystem.h"
#include "../../../../../../kernel/subsystems/io/file/embedded/embedded.h"
#include "../../../../fixtures/process.h"

static FilesystemPtr root = NULL;
static FilesystemPtr embedded_binaries = NULL;

static EmbeddedFilesystemIOStatePtr embedded_binaries_state = NULL;
static FilesystemIOStatePtr root_state = NULL;

static Scheduler scheduler = NEW_SCHEDULER;
static IOSubsystem io_subsystem = NEW_IO_SUBSYSTEM(&scheduler);

static Optional(ProcessPtr) dummy_proc = None(ProcessPtr);
static KernelContext kernel_context = EMPTY_KERNEL_CONTEXT;

void setUp(void)
{
    root = RootFilesystem__heap_new();
    root_state = FilesystemIOState__heap_new(root);
    dummy_proc = Some(ProcessPtr, dummy_process(100, 11, PROC_STATE_RUNNING));

    embedded_binaries = Filesystem__heap_new(kstring("embed"), EmbeddedFilesystemIO__new());
    embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(root, kstring("embedded"), embedded_binaries);

    kernel_context = KernelContext__new(dummy_proc, &scheduler);

    Result(DeviceHandle) register_res = IOSubsystem__register_device(
        &io_subsystem, kstring("/"), &root->file_io.base_io, &root->state->state
    );

    ResultMatch(DeviceHandle, register_res)
        IsError(code, message, TEST_FAIL())
    End
}

void tearDown(void)
{

}

static KString embedded = KSTRING_NEW("embedded");
static KString hello_world = KSTRING_NEW("hello_world");

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
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, kstring("/embedded/"));

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
    Result(IOHandle) handle = IOSubsystem__open(&io_subsystem, dummy_proc, kstring("/embedded/hello_world"), 0);

    ResultMatch(IOHandle, handle)
        IsError(code, message,
        {
            k_printf(cstr(message));
            TEST_FAIL();
        })
        IsSuccess(hand, {
            Char buffer[4096];
            IOContext io_context = IOContext__new(kernel_context, &io_subsystem, dummy_proc, hand, buffer, 4096);
            Result(ByteCount) res = IOSubsystem__read(&io_subsystem, io_context);

            ResultMatch(ByteCount, res)
                IsError(code, message,
                {
                    k_println(cstr(message));
                    TEST_FAIL();
                })
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(4096, bytes))
            End
        });
    End
}

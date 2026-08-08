#include "unity.h"
#include <string.h>

#include "../../../../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../../../../kernel/syscall/io_handler.h"
#include "../../../../../../kernel/process.h"
#include "../../../../../../kernel/print.h"
#include "../../../../../../kernel/scheduler/scheduler.h"
#include "../../../../../../kernel/subsystems/io/block/block_device.h"
#include "../../../../../../kernel/subsystems/io/block/memory_block_device.h"
#include "../../../../../../kernel/subsystems/io/file/filesystem.h"
#include "../../../../../../kernel/subsystems/io/file/fat16/fat16.h"
#include "../../../../../../kernel/subsystems/io/file/fat32/fat32.h"
#include "../../../../../../kernel/subsystems/io/file/memfs/memfs.h"
#include "../../../../../../kernel/subsystems/io/file/register.h"
#include "../../../../fixtures/io_subsystem.h"
#include "../../../../fixtures/process.h"
#include "../../../../fixtures/kernel.h"

static FilesystemPtr root = NULL;
static IOStatePtr root_state = NULL;
static MemoryBlockDevice fat16_memory_device;
static MemoryBlockDevice fat32_memory_device;
static BlockDevice fat16_block_device;
static BlockDevice fat32_block_device;

static KernelPtr kernel = NULL;
static ProcessPtr dummy_proc = NULL;

static KernelContext kernel_context = EMPTY_KERNEL_CONTEXT;

static BString async_list_pending = BSTRING_NEW("Async list pending");
static BString async_invalid_path = BSTRING_NEW("Invalid path");

typedef struct AsyncListDirFilesystemIOState
{
    IOState state;
    bool captured_context;
    KernelContext continuation_context;
}
AsyncListDirFilesystemIOState;

typedef AsyncListDirFilesystemIOState * AsyncListDirFilesystemIOStatePtr;

static Result(DirectoryEntryCount) AsyncListDirFilesystemIO__list_dir(
    IOStatePtr self, BString path, DirectoryEntryPtr entries, size_t max_entries
)
{
    AsyncListDirFilesystemIOStatePtr state = ptr(self, AsyncListDirFilesystemIOState);
    Match(KernelContextPtr, BlockDevice__current_context())
        IsSome(current_context,
        {
            state->captured_context = true;
            state->continuation_context = *current_context;
        })
        IsNone(state->captured_context = false)
    End

    return Error(DirectoryEntryCount, async_list_pending, BLOCK_DEVICE_REQUEST_PENDING);
}

static FileIO AsyncListDirFilesystemIO__new(void)
{
    return (FileIO) {
        .base_io = (IO) { 0 },
        .list_nodes = NULL,
        .list_dir = AsyncListDirFilesystemIO__list_dir,
        .mkdir = NULL,
        .remove = NULL,
        .link = NULL,
    };
}

static AsyncListDirFilesystemIOStatePtr AsyncListDirFilesystemIOState__heap_new(FilesystemPtr filesystem)
{
    AsyncListDirFilesystemIOStatePtr state =
        ptr(Kernel__alloc_kernel(sizeof(AsyncListDirFilesystemIOState)), AsyncListDirFilesystemIOState);
    *state = (AsyncListDirFilesystemIOState) {
        .state = { 0 },
        .captured_context = false,
        .continuation_context = EMPTY_KERNEL_CONTEXT,
    };
    filesystem->state = ptr(state, FilesystemIOState);
    return state;
}

void setUp(void)
{
    dummy_proc = dummy_process(100, 11, PROC_STATE_RUNNING);

    root = init_root_filesystem_with_embedded_binaries();
    fat16_memory_device = MemoryBlockDevice__new();
    fat32_memory_device = MemoryBlockDevice__new();
    fat16_block_device = MemoryBlockDevice__as_block_device(&fat16_memory_device);
    fat32_block_device = MemoryBlockDevice__as_block_device(&fat32_memory_device);

    FilesystemPtr fat16_filesystem = Filesystem__heap_new(bstring("fat16"), Fat16FilesystemIO__new());
    Fat16FilesystemIOStatePtr fat16_state = Fat16FilesystemIOState__heap_new(
        fat16_filesystem, &fat16_block_device, 0
    );
    Filesystem__mount(root, bstring("fat16"), fat16_filesystem);

    FilesystemPtr fat32_filesystem = Filesystem__heap_new(bstring("fat32"), Fat32FilesystemIO__new());
    Fat32FilesystemIOStatePtr fat32_state = Fat32FilesystemIOState__heap_new(
        fat32_filesystem, &fat32_block_device, 0
    );
    Filesystem__mount(root, bstring("fat32"), fat32_filesystem);

    kernel = init_test_kernel_with_filesystem(root);
    root_state = &root->state->state;

    kernel_context = KernelContext__new(Some(ProcessID, dummy_proc->pid), Some(SchedulerPtr, kernel->scheduler));
}

void tearDown(void)
{

}

static BString embedded = BSTRING_NEW("sbin");
static BString dev = BSTRING_NEW("dev");
static BString tmp = BSTRING_NEW("tmp");
static BString memfs = BSTRING_NEW("memfs");
static BString fat16 = BSTRING_NEW("fat16");
static BString fat32 = BSTRING_NEW("fat32");
static BString hello_world = BSTRING_NEW("hello_world");
static BString corrupt = BSTRING_NEW("corrupt");

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

static BString numbered_memfs_path(int index)
{
    char path[] = "/tmp/memfs/file00";
    path[15] = '0' + (index / 10);
    path[16] = '0' + (index % 10);
    return bstring(path);
}


void test_it_can_list_files_from_root(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, bstring("/"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(5, FilesystemNodes__len(&result));
            FilesystemNodePtr node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 0), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&embedded, &node->name));
            FilesystemNodePtr dev_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 1), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&dev, &dev_node->name));
            FilesystemNodePtr tmp_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 2), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&tmp, &tmp_node->name));
            FilesystemNodePtr fat16_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 3), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&fat16, &fat16_node->name));
            FilesystemNodePtr fat32_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 4), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&fat32, &fat32_node->name));
        });
    End
}

void test_devfs_lists_registered_block_devices(void)
{
    ProcessPtr service = dummy_process(200, 11, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;

    ResultMatch(BlockDeviceCode, BlockDeviceRegistry__register(BSTRING_NEW("/dev/virtio1"), service, 128, kernel->message_broker))
        IsSuccess(code, TEST_ASSERT_EQUAL(BLOCK_DEVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, bstring("/dev"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message, TEST_FAIL())
        IsSuccess(result,
        {
            TEST_ASSERT_EQUAL_INT(2, FilesystemNodes__len(&result));

            FilesystemNodePtr driver_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 0), (FilesystemNodePtr){});
            BString expected_driver = bstring("virtio1");
            TEST_ASSERT_TRUE(BString__equals(&expected_driver, &driver_node->name));

            FilesystemNodePtr metadata_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 1), (FilesystemNodePtr){});
            BString expected_metadata = bstring("hd0");
            TEST_ASSERT_TRUE(BString__equals(&expected_metadata, &metadata_node->name));
        })
    End
}

void test_devfs_can_open_and_read_block_device_metadata(void)
{
    ProcessPtr service = dummy_process(200, 11, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;

    ResultMatch(BlockDeviceCode, BlockDeviceRegistry__register(BSTRING_NEW("/dev/virtio1"), service, 128, kernel->message_broker))
        IsSuccess(code, TEST_ASSERT_EQUAL(BLOCK_DEVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) metadata_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/dev/hd0"), IO_OPEN_READ);

    ResultMatch(IOHandle, metadata_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(handle,
        {
            char buffer[128];
            ResultMatch(ByteCount, IOSubsystem__read(kernel->io_subsystem, rw_context(handle, (DataPtr) buffer, sizeof(buffer))))
                IsError(code, message, TEST_FAIL())
                IsSuccess(bytes,
                {
                    buffer[bytes] = '\0';
                    TEST_ASSERT_EQUAL_STRING(
                        "path=/dev/virtio1\nblock_size=512\nblock_count=128\nuserspace_service=1\n",
                        buffer
                    );
                })
            End
        })
    End
}

void test_it_can_list_files_to_subdir(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, bstring("/sbin/"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(3, FilesystemNodes__len(&result));
            FilesystemNodePtr node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 0), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&hello_world, &node->name));
        });
    End
}

void test_it_can_list_nested_mount_parent(void)
{
    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, bstring("/tmp"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(result, {
            TEST_ASSERT_EQUAL_INT(1, FilesystemNodes__len(&result));
            FilesystemNodePtr node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 0), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&memfs, &node->name));
        });
    End
}

void test_filesystem_table_adds_mount_entries(void)
{
    FilesystemTable table = FilesystemTable__new();
    BString device_path = bstring("/dev/virtio1");
    BString filesystem_type = bstring("fat32");
    BString mountpoint = bstring("/mnt/fs1");

    ResultMatch(CollectionCode, FilesystemTable__add(
        &table,
        device_path,
        filesystem_type,
        mountpoint
    ))
        IsSuccess(index, TEST_ASSERT_EQUAL(0, index))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(1, FilesystemTableEntries__len(&table.entries));
    Match(FilesystemTableEntries__type_ptr__optional, FilesystemTableEntries__ptr_at(&table.entries, 0))
        IsSome(entry,
        {
            TEST_ASSERT_TRUE(BString__equals(&entry->device_path, &device_path));
            TEST_ASSERT_TRUE(BString__equals(&entry->filesystem_type, &filesystem_type));
            TEST_ASSERT_TRUE(BString__equals(&entry->mountpoint, &mountpoint));
        })
        IsNone(TEST_FAIL())
    End
}

void test_it_can_open_files_and_read_from_embedded_via_io_subsystem(void)
{
    Result(IOHandle) handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/sbin/hello_world"), 0);

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
            TEST_ASSERT_EQUAL_INT(5, count);
            TEST_ASSERT_EQUAL_STRING("sbin", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("dev", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("tmp", entries[2].name);
            TEST_ASSERT_EQUAL_STRING("fat16", entries[3].name);
            TEST_ASSERT_EQUAL_STRING("fat32", entries[4].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_backing_root_entries_and_mount_overlays(void)
{
    FilesystemPtr backing_root = Filesystem__heap_new(bstring("root-memfs"), MemFilesystemIO__new());
    MemFilesystemIOState__heap_new(backing_root);
    Filesystem__mount(root, bstring("/"), backing_root);

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/docs", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/hello.txt"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            char data[] = "root";
            ResultMatch(ByteCount, IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr) data, 4)))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(4, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    DirectoryEntry entries[8] = { 0 };
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/", (Anything) entries, 8, 0
    ))
        IsSuccess(count,
        {
            bool saw_docs = false;
            bool saw_hello = false;
            bool saw_ebin = false;
            bool saw_dev = false;
            bool saw_tmp = false;
            bool saw_fat16 = false;
            bool saw_fat32 = false;

            TEST_ASSERT_EQUAL_INT(7, count);
            for (DirectoryEntryCount i = 0; i < count; i++)
            {
                if (strcmp(entries[i].name, "docs") == 0)
                {
                    saw_docs = true;
                }
                else if (strcmp(entries[i].name, "hello.txt") == 0)
                {
                    saw_hello = true;
                }
                else if (strcmp(entries[i].name, "sbin") == 0)
                {
                    saw_ebin = true;
                }
                else if (strcmp(entries[i].name, "dev") == 0)
                {
                    saw_dev = true;
                }
                else if (strcmp(entries[i].name, "tmp") == 0)
                {
                    saw_tmp = true;
                }
                else if (strcmp(entries[i].name, "fat16") == 0)
                {
                    saw_fat16 = true;
                }
                else if (strcmp(entries[i].name, "fat32") == 0)
                {
                    saw_fat32 = true;
                }
            }

            TEST_ASSERT_TRUE(saw_docs);
            TEST_ASSERT_TRUE(saw_hello);
            TEST_ASSERT_TRUE(saw_ebin);
            TEST_ASSERT_TRUE(saw_dev);
            TEST_ASSERT_TRUE(saw_tmp);
            TEST_ASSERT_TRUE(saw_fat16);
            TEST_ASSERT_TRUE(saw_fat32);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_root_async_list_dir_merges_mount_overlays_on_completion(void)
{
    FilesystemPtr async_root = Filesystem__heap_new(bstring("async-root"), AsyncListDirFilesystemIO__new());
    AsyncListDirFilesystemIOStatePtr async_state = AsyncListDirFilesystemIOState__heap_new(async_root);
    Filesystem__mount(root, bstring("/"), async_root);

    DirectoryEntry entries[8] = { 0 };

    BlockDevice__set_current_context(&kernel_context);
    ResultMatch(DirectoryEntryCount, root->file_io.list_dir(root_state, bstring("/"), entries, 8))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_TRUE(async_state->captured_context);
    TEST_ASSERT_TRUE(async_state->continuation_context.result_mapper.present);

    strcpy(entries[0].name, "docs");
    entries[0].name_len = 4;
    strcpy(entries[1].name, "hello.txt");
    entries[1].name_len = 9;

    Match(KernelResultMapper, async_state->continuation_context.result_mapper)
        IsSome(mapper,
        {
            ResultMatch(Anything, mapper(Success(Anything, (Anything) 2), async_state->continuation_context.result_mapper_context))
                IsSuccess(value,
                {
                    DirectoryEntryCount count = (DirectoryEntryCount) value;
                    bool saw_docs = false;
                    bool saw_hello = false;
                    bool saw_ebin = false;
                    bool saw_dev = false;
                    bool saw_tmp = false;
                    bool saw_fat16 = false;
                    bool saw_fat32 = false;

                    TEST_ASSERT_EQUAL_INT(7, count);
                    for (DirectoryEntryCount i = 0; i < count; i++)
                    {
                        if (strcmp(entries[i].name, "docs") == 0)
                        {
                            saw_docs = true;
                        }
                        else if (strcmp(entries[i].name, "hello.txt") == 0)
                        {
                            saw_hello = true;
                        }
                        else if (strcmp(entries[i].name, "sbin") == 0)
                        {
                            saw_ebin = true;
                        }
                        else if (strcmp(entries[i].name, "dev") == 0)
                        {
                            saw_dev = true;
                        }
                        else if (strcmp(entries[i].name, "tmp") == 0)
                        {
                            saw_tmp = true;
                        }
                        else if (strcmp(entries[i].name, "fat16") == 0)
                        {
                            saw_fat16 = true;
                        }
                        else if (strcmp(entries[i].name, "fat32") == 0)
                        {
                            saw_fat32 = true;
                        }
                    }

                    TEST_ASSERT_TRUE(saw_docs);
                    TEST_ASSERT_TRUE(saw_hello);
                    TEST_ASSERT_TRUE(saw_ebin);
                    TEST_ASSERT_TRUE(saw_dev);
                    TEST_ASSERT_TRUE(saw_tmp);
                    TEST_ASSERT_TRUE(saw_fat16);
                    TEST_ASSERT_TRUE(saw_fat32);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
        IsNone(TEST_FAIL())
    End
}

void test_root_async_list_dir_synthesizes_mount_parent_on_backing_invalid_path(void)
{
    FilesystemPtr async_root = Filesystem__heap_new(bstring("async-root"), AsyncListDirFilesystemIO__new());
    AsyncListDirFilesystemIOStatePtr async_state = AsyncListDirFilesystemIOState__heap_new(async_root);
    Filesystem__mount(root, bstring("/"), async_root);

    DirectoryEntry tmp_entries[4] = { 0 };
    DirectoryEntry mnt_entries[4] = { 0 };

    BlockDevice__set_current_context(&kernel_context);
    ResultMatch(DirectoryEntryCount, root->file_io.list_dir(root_state, bstring("/tmp"), tmp_entries, 4))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_TRUE(async_state->captured_context);
    TEST_ASSERT_TRUE(async_state->continuation_context.result_mapper.present);

    Match(KernelResultMapper, async_state->continuation_context.result_mapper)
        IsSome(tmp_mapper,
        {
            ResultMatch(Anything, tmp_mapper(
                Error(Anything, async_invalid_path, IO_INVALID_PATH),
                async_state->continuation_context.result_mapper_context
            ))
                IsSuccess(value,
                {
                    DirectoryEntryCount count = (DirectoryEntryCount) value;
                    TEST_ASSERT_EQUAL_INT(1, count);
                    TEST_ASSERT_EQUAL_STRING("memfs", tmp_entries[0].name);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
        IsNone(TEST_FAIL())
    End

    BlockDevice__set_current_context(&kernel_context);
    ResultMatch(DirectoryEntryCount, root->file_io.list_dir(root_state, bstring("/mnt"), mnt_entries, 4))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_TRUE(async_state->captured_context);
    TEST_ASSERT_TRUE(async_state->continuation_context.result_mapper.present);

    Match(KernelResultMapper, async_state->continuation_context.result_mapper)
        IsSome(mnt_mapper,
        {
            ResultMatch(Anything, mnt_mapper(
                Error(Anything, async_invalid_path, IO_INVALID_PATH),
                async_state->continuation_context.result_mapper_context
            ))
                IsSuccess(value,
                {
                    DirectoryEntryCount count = (DirectoryEntryCount) value;
                    TEST_ASSERT_EQUAL_INT(1, count);
                    TEST_ASSERT_EQUAL_STRING("fs1", mnt_entries[0].name);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
        IsNone(TEST_FAIL())
    End
}

void test_build_root_and_mounts_lists_backing_root_entries_and_mount_overlays(void)
{
    FilesystemPtr built_root = Filesystem__build_root_and_mounts();
    FilesystemPtr backing_root = Filesystem__heap_new(bstring("root-memfs"), MemFilesystemIO__new());
    MemFilesystemIOState__heap_new(backing_root);
    Filesystem__mount(built_root, bstring("/"), backing_root);

    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(built_root);
    KernelContext mounted_context =
        KernelContext__new(Some(ProcessID, dummy_proc->pid), Some(SchedulerPtr, mounted_kernel->scheduler));
    IOContextPtr mounted_open_context = IOContext__heap_new(
        mounted_context,
        Some(IOSubsystemPtr, mounted_kernel->io_subsystem),
        None(IOHandle),
        None(ExternalBuffer)
    );

    ResultMatch(SystemCallResult, IO__handler(
        mounted_kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/docs", 0, 0, 0
    ))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    Result(IOHandle) write_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, mounted_open_context, bstring("/hello.txt"), IO_OPEN_WRITE);

    ResultMatch(IOHandle, write_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(write_hand,
        {
            ExternalBuffer ext_buffer = ExternalBuffer__new((DataPtr) "root", 4);
            IOContextPtr mounted_rw_context = IOContext__heap_new(
                mounted_context,
                Some(IOSubsystemPtr, mounted_kernel->io_subsystem),
                Some(IOHandle, write_hand),
                Some(ExternalBuffer, ext_buffer)
            );
            IOContextPtr mounted_close_context = IOContext__heap_new(
                mounted_context,
                Some(IOSubsystemPtr, mounted_kernel->io_subsystem),
                Some(IOHandle, write_hand),
                None(ExternalBuffer)
            );

            ResultMatch(ByteCount, IOSubsystem__write(mounted_kernel->io_subsystem, mounted_rw_context))
                IsSuccess(bytes, TEST_ASSERT_EQUAL_INT(4, bytes))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(IOHandle, IOSubsystem__close(mounted_kernel->io_subsystem, mounted_close_context))
                IsSuccess(handle, TEST_ASSERT_EQUAL(write_hand, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
    End

    DirectoryEntry root_entries[8] = { 0 };
    ResultMatch(SystemCallResult, IO__handler(
        mounted_kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/", (Anything) root_entries, 8, 0
    ))
        IsSuccess(count,
        {
            bool saw_docs = false;
            bool saw_hello = false;
            bool saw_ebin = false;
            bool saw_dev = false;
            bool saw_tmp = false;

            TEST_ASSERT_EQUAL_INT(5, count);
            for (DirectoryEntryCount i = 0; i < count; i++)
            {
                if (strcmp(root_entries[i].name, "docs") == 0)
                {
                    saw_docs = true;
                }
                else if (strcmp(root_entries[i].name, "hello.txt") == 0)
                {
                    saw_hello = true;
                }
                else if (strcmp(root_entries[i].name, "sbin") == 0)
                {
                    saw_ebin = true;
                }
                else if (strcmp(root_entries[i].name, "dev") == 0)
                {
                    saw_dev = true;
                }
                else if (strcmp(root_entries[i].name, "tmp") == 0)
                {
                    saw_tmp = true;
                }
            }

            TEST_ASSERT_TRUE(saw_docs);
            TEST_ASSERT_TRUE(saw_hello);
            TEST_ASSERT_TRUE(saw_ebin);
            TEST_ASSERT_TRUE(saw_dev);
            TEST_ASSERT_TRUE(saw_tmp);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_build_root_and_mounts_lists_tmp_mount_parent_when_backing_root_is_mounted(void)
{
    FilesystemPtr built_root = Filesystem__build_root_and_mounts();
    FilesystemPtr backing_root = Filesystem__heap_new(bstring("root-memfs"), MemFilesystemIO__new());
    MemFilesystemIOState__heap_new(backing_root);
    Filesystem__mount(built_root, bstring("/"), backing_root);

    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(built_root);

    DirectoryEntry tmp_entries[4] = { 0 };
    ResultMatch(SystemCallResult, IO__handler(
        mounted_kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/tmp", (Anything) tmp_entries, 4, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("memfs", tmp_entries[0].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_lists_ebin_files(void)
{
    DirectoryEntry entries[4];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/sbin", (Anything) entries, 4, 0
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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat16/NOTE.TXT"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat16/NOTE.TXT"), IO_OPEN_READ);

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
        IsSuccess(count, TEST_ASSERT_EQUAL_INT(5, count))
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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/NOTE.TXT"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/NOTE.TXT"), IO_OPEN_READ);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/bin/file.txt"), IO_OPEN_WRITE);
    ResultMatch(IOHandle, first)
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End

    Result(IOHandle) second =
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/bin/file.txt"), IO_OPEN_WRITE);
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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/bin/file.txt"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/bin/file.txt"), IO_OPEN_READ);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/notes"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/notes"), IO_OPEN_READ);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/docs/notes"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/dir/file.txt"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/dir/file.txt"), IO_OPEN_READ);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/dir/file.txt"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/fat32/dir/file.txt"), IO_OPEN_READ);

    ResultMatch(IOHandle, read_handle)
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_memfs_rm_removes_file_from_directory(void)
{
    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/remove-me"), IO_OPEN_WRITE))
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

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/remove-me"), IO_OPEN_READ))
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

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/docs/nested/notes"), IO_OPEN_WRITE))
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

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/docs/nested/notes"), IO_OPEN_READ))
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code))
    End
}

void test_mkdir_and_rm_on_embeddedfs_return_unsupported(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, (Anything) "/sbin/newdir", 0, 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, (Anything) "/sbin/hello_world", 0, 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End
}

void test_memfs_file_can_grow_beyond_one_page(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/big"), IO_OPEN_WRITE);

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
        IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/big"), IO_OPEN_READ);

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
    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/corrupt"), IO_OPEN_WRITE))
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

    Match(FilesystemPtr, Mounts__get(&root->mounts, bstring("tmp/memfs")))
        IsNone(TEST_FAIL())
        IsSome(mounted_fs,
        {
            MemFilesystemIOStatePtr mem_state = (MemFilesystemIOStatePtr) mounted_fs->state;
            LLIterate(MemFiles, mem_state->files, file)
                if (BString__equals(&file.name, &corrupt))
                {
                    file__node->value.pages = MemFilePages__new();
                    file__node->value.capacity = PAGE_SIZE;
                    file__node->value.size = sizeof("data") - 1;
                }
            End
        })
    End

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/corrupt"), IO_OPEN_READ))
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
    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/truncate"), IO_OPEN_WRITE))
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

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/truncate"), IO_OPEN_WRITE))
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

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, open_context(), bstring("/tmp/memfs/truncate"), IO_OPEN_READ))
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
        BString path = numbered_memfs_path(i);
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

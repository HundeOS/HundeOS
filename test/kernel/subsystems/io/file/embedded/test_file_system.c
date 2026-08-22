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
#include "../../../../../../kernel/subsystems/io/stream/stream.h"
#include "../../../../../../kernel/system_info.h"
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
static SystemInfo fake_system_info = {
    .cpu_count = 4,
    .physical_memory = {
        .physical_start = 0,
        .physical_end = 0x100000000,
    },
    .kernel_heap = {
        .virtual_start = 0xfffff00000000000,
        .physical_start = 0x90200000,
        .physical_end = 0x98200000,
    },
    .uart = {
        .physical_base = 0x12345000,
        .virtual_base = 0xffff000000000000,
        .irq = 99,
    },
    .interrupt_controller = {
        .kind = INTERRUPT_CONTROLLER_GIC_V3,
        .distributor_physical_base = 0x08000000,
        .redistributor_physical_base = 0x080a0000,
    },
    .pci = Some(PciInfo, ((PciInfo) {
        .ecam_physical_base = 0x4010000000,
        .ecam_size = 0x10000000,
        .bus_start = 0,
        .bus_end = 255,
    })),
    .sd_host = Some(SdHostInfo, ((SdHostInfo) {
        .physical_base = 0xfe340000,
        .mmio_size = 0x100,
        .irq = 126,
    })),
};

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
    BlockDeviceRegistry__reset();
    dummy_proc = dummy_process(100, 11, PROC_STATE_RUNNING);

    kernel = init_test_kernel();
    root = Unwrap(FilesystemPtr, kernel->io_subsystem->root_filesystem, (FilesystemPtr){});

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

    kernel->system_info = Some(SystemInfo, fake_system_info);
    root_state = &root->state->state;

    kernel_context = KernelContext__new(Some(ProcessID, dummy_proc->pid), Some(SchedulerPtr, kernel->scheduler));
}

void tearDown(void)
{

}

static BString embedded = BSTRING_NEW("sbin");
static BString dev = BSTRING_NEW("dev");
static BString proc = BSTRING_NEW("proc");
static BString tmp = BSTRING_NEW("tmp");
static BString memfs = BSTRING_NEW("memfs");
static BString fat16 = BSTRING_NEW("fat16");
static BString fat32 = BSTRING_NEW("fat32");
static BString hello_world = BSTRING_NEW("hello_world");
static BString corrupt = BSTRING_NEW("corrupt");

static const uint64_t HELLO_WORLD_SIZE = 66840;

static bool FilesystemNodes__has_name(FilesystemNodes * self, BString name)
{
    FAPtrIterate(FilesystemNodes, self, index, node)
        if (BString__equals(&name, &node->name))
        {
            return true;
        }
    End

    return false;
}

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
            TEST_ASSERT_EQUAL_INT(6, FilesystemNodes__len(&result));
            FilesystemNodePtr node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 0), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&embedded, &node->name));
            FilesystemNodePtr dev_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 1), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&dev, &dev_node->name));
            FilesystemNodePtr proc_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 2), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&proc, &proc_node->name));
            FilesystemNodePtr tmp_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 3), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&tmp, &tmp_node->name));
            FilesystemNodePtr fat16_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 4), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&fat16, &fat16_node->name));
            FilesystemNodePtr fat32_node = Unwrap(FilesystemNodePtr, FilesystemNodes__at(&result, 5), (FilesystemNodePtr){});
            TEST_ASSERT_TRUE(BString__equals(&fat32, &fat32_node->name));
        });
    End
}

void test_devfs_lists_registered_block_devices(void)
{
    ProcessPtr service = dummy_process(200, 11, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;

    ResultMatch(DeviceHandle, IOSubsystem__register_device(
        kernel->io_subsystem,
        bstring("/dev/businfo"),
        (IOPtr) Stream__heap_new(),
        (IOStatePtr) StreamState__heap_new()
    ))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(BlockDeviceCode, BlockDeviceRegistry__register(BSTRING_NEW("/dev/virtio1"), service, 128, kernel->message_broker))
        IsSuccess(code, TEST_ASSERT_EQUAL(BLOCK_DEVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    Result(FilesystemNodes) nodes = root->file_io.list_nodes(root_state, NULL, bstring("/dev"));

    ResultMatch(FilesystemNodes, nodes)
        IsError(code, message, TEST_FAIL())
        IsSuccess(result,
        {
            TEST_ASSERT_EQUAL_INT(3, FilesystemNodes__len(&result));
            TEST_ASSERT_TRUE(FilesystemNodes__has_name(&result, bstring("businfo")));
            TEST_ASSERT_TRUE(FilesystemNodes__has_name(&result, bstring("virtio1")));
            TEST_ASSERT_TRUE(FilesystemNodes__has_name(&result, bstring("hd0")));
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

void test_procfs_can_open_and_read_system_info(void)
{
    KernelPtr procfs_kernel = init_test_kernel_without_filesystem();
    procfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    SystemInfoPtr system_info = &procfs_kernel->system_info.some.val;
    for (uint64_t index = 0; index < 12; index++)
    {
        PhysicalMemoryRegion region = {
            .physical_start = 0x40000000 + (index * 0x1000000),
            .physical_end = 0x40000000 + ((index + 1) * 0x1000000),
        };
        PhysicalMemoryRegions__push(&system_info->physical_memory.free_regions, region);
    }

    FilesystemPtr procfs_root = Filesystem__build_root_and_mounts(procfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(procfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), bstring("/proc/sysinfo"), IO_OPEN_READ);

    ResultMatch(IOHandle, sysinfo_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(handle,
        {
            char buffer[4096];
            ByteCount total = 0;
            bool eof = false;
            while (!eof && total < sizeof(buffer) - 1)
            {
                ResultMatch(ByteCount, IOSubsystem__read(
                    mounted_kernel->io_subsystem,
                    rw_context(handle, (DataPtr) &buffer[total], 128)
                ))
                    IsError(code, message,
                    {
                        if (code == IO_EOF)
                        {
                            eof = true;
                        }
                        else
                        {
                            TEST_FAIL();
                        }
                    })
                    IsSuccess(bytes, total += bytes;)
                End
            }
            buffer[total] = '\0';

            TEST_ASSERT_TRUE(eof);
            TEST_ASSERT_GREATER_THAN(1024, total);
            TEST_ASSERT_NOT_NULL(strstr(buffer, "cpu.count=4\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.physical_start=0x0\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.physical_end=0x100000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.physical_size=0x100000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.physical_size_human=4 GiB\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.free_region.count=12\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.free_region.11.physical_start=0x4b000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.free_region.11.physical_end=0x4c000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.free_region.11.physical_size_human=16 MiB\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.kernel_heap.physical_start=0x90200000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.kernel_heap.physical_end=0x98200000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.kernel_heap.physical_size=0x8000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.kernel_heap.physical_size_human=128 MiB\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "uart.physical_base=0x12345000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "uart.irq=99\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "uart.mmio_size=0x1000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.present=0\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "pci.present=1\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "pci.ecam.physical_base=0x4010000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "pci.ecam.size=0x10000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "pci.ecam.size_human=256 MiB\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "pci.bus.start=0\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "pci.bus.end=255\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "sd_host.present=1\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "sd_host.physical_base=0xfe340000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "sd_host.mmio_size=0x100\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "sd_host.irq=126\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "interrupt_controller.kind=2\n"));
            TEST_ASSERT_NOT_NULL(strstr(
                buffer,
                "interrupt_controller.distributor_physical_base=0x8000000\n"
            ));
        })
    End
}

void test_procfs_can_read_system_info_from_non_zero_offset(void)
{
    KernelPtr procfs_kernel = init_test_kernel_without_filesystem();
    procfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    SystemInfoPtr system_info = &procfs_kernel->system_info.some.val;
    for (uint64_t index = 0; index < 12; index++)
    {
        PhysicalMemoryRegion region = {
            .physical_start = 0x40000000 + (index * 0x1000000),
            .physical_end = 0x40000000 + ((index + 1) * 0x1000000),
        };
        PhysicalMemoryRegions__push(&system_info->physical_memory.free_regions, region);
    }

    FilesystemPtr procfs_root = Filesystem__build_root_and_mounts(procfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(procfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), bstring("/proc/sysinfo"), IO_OPEN_READ);

    ResultMatch(IOHandle, sysinfo_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(handle,
        {
            char skipped[128];
            ResultMatch(ByteCount, IOSubsystem__read(
                mounted_kernel->io_subsystem, rw_context(handle, (DataPtr) skipped, sizeof(skipped))
            ))
                IsError(code, message, TEST_FAIL())
                IsSuccess(bytes, TEST_ASSERT_EQUAL_UINT64(128, bytes);)
            End

            char buffer[4096];
            ByteCount total = 0;
            bool eof = false;
            while (!eof && total < sizeof(buffer) - 1)
            {
                ResultMatch(ByteCount, IOSubsystem__read(
                    mounted_kernel->io_subsystem,
                    rw_context(handle, (DataPtr) &buffer[total], 128)
                ))
                    IsError(code, message,
                    {
                        if (code == IO_EOF)
                        {
                            eof = true;
                        }
                        else
                        {
                            TEST_FAIL();
                        }
                    })
                    IsSuccess(bytes, total += bytes;)
                End
            }
            buffer[total] = '\0';

            TEST_ASSERT_TRUE(eof);
            TEST_ASSERT_GREATER_THAN(896, total);
            TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.free_region.11.physical_end=0x4c000000\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "interrupt_controller.kind=2\n"));
        })
    End
}

void test_procfs_can_read_framebuffer_system_info(void)
{
    KernelPtr procfs_kernel = init_test_kernel_without_filesystem();
    SystemInfo framebuffer_system_info = fake_system_info;
    framebuffer_system_info.framebuffer = Some(FramebufferInfo, ((FramebufferInfo) {
        .physical_base = 0x3e000000,
        .size = 0x7e9000,
        .width = 1920,
        .height = 1080,
        .pixels_per_scanline = 2048,
        .pixel_format = FRAMEBUFFER_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR,
        .red_mask = 0x00ff0000,
        .green_mask = 0x0000ff00,
        .blue_mask = 0x000000ff,
        .reserved_mask = 0xff000000,
    }));
    procfs_kernel->system_info = Some(SystemInfo, framebuffer_system_info);

    FilesystemPtr procfs_root = Filesystem__build_root_and_mounts(procfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(procfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), bstring("/proc/sysinfo"), IO_OPEN_READ);

    ResultMatch(IOHandle, sysinfo_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(handle,
        {
            char buffer[2048];
            ResultMatch(ByteCount, IOSubsystem__read(
                mounted_kernel->io_subsystem,
                rw_context(handle, (DataPtr) buffer, sizeof(buffer) - 1)
            ))
                IsError(code, message, TEST_FAIL())
                IsSuccess(bytes,
                {
                    buffer[bytes] = '\0';

                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.present=1\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.physical_base=0x3e000000\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.physical_size=0x7e9000\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.physical_size_human=8100 KiB\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.width=1920\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.height=1080\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.pixels_per_scanline=2048\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.pixel_format=1\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.red_mask=0xff0000\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.green_mask=0xff00\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.blue_mask=0xff\n"));
                    TEST_ASSERT_NOT_NULL(strstr(buffer, "framebuffer.reserved_mask=0xff000000\n"));
                })
            End
        })
    End
}

void test_procfs_returns_eof_after_streaming_system_info(void)
{
    KernelPtr procfs_kernel = init_test_kernel_without_filesystem();
    procfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr procfs_root = Filesystem__build_root_and_mounts(procfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(procfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), bstring("/proc/sysinfo"), IO_OPEN_READ);

    ResultMatch(IOHandle, sysinfo_handle)
        IsError(code, message, TEST_FAIL())
        IsSuccess(handle,
        {
            char buffer[2048];
            ResultMatch(ByteCount, IOSubsystem__read(
                mounted_kernel->io_subsystem,
                rw_context(handle, (DataPtr) buffer, sizeof(buffer))
            ))
                IsError(code, message, TEST_FAIL())
                IsSuccess(bytes, {})
            End

            ResultMatch(ByteCount, IOSubsystem__read(
                mounted_kernel->io_subsystem,
                rw_context(handle, (DataPtr) buffer, sizeof(buffer))
            ))
                IsError(code, message,
                {
                    TEST_ASSERT_EQUAL_INT64(IO_EOF, code);
                })
                IsSuccess(bytes, TEST_FAIL())
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
            TEST_ASSERT_EQUAL_INT(7, FilesystemNodes__len(&result));
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
            TEST_ASSERT_FALSE(entry->match_block_count);
            TEST_ASSERT_EQUAL_UINT64(0, entry->block_count);
        })
        IsNone(TEST_FAIL())
    End
}

void test_filesystem_table_adds_block_count_mount_entries(void)
{
    FilesystemTable table = FilesystemTable__new();
    uint64_t block_count = 16384;
    BString filesystem_type = bstring("ext2");
    BString mountpoint = bstring("/boot/config");

    ResultMatch(CollectionCode, FilesystemTable__add_by_block_count(
        &table,
        block_count,
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
            TEST_ASSERT_TRUE(BString__equals(&entry->device_path, &EMPTY_BSTRING));
            TEST_ASSERT_TRUE(BString__equals(&entry->filesystem_type, &filesystem_type));
            TEST_ASSERT_TRUE(BString__equals(&entry->mountpoint, &mountpoint));
            TEST_ASSERT_TRUE(entry->match_block_count);
            TEST_ASSERT_EQUAL_UINT64(block_count, entry->block_count);
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
    DirectoryEntry entries[6];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/", (Anything) entries, 6, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(6, count);
            TEST_ASSERT_EQUAL_STRING("sbin", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("dev", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("proc", entries[2].name);
            TEST_ASSERT_EQUAL_STRING("tmp", entries[3].name);
            TEST_ASSERT_EQUAL_STRING("fat16", entries[4].name);
            TEST_ASSERT_EQUAL_STRING("fat32", entries[5].name);
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
            bool saw_proc = false;
            bool saw_tmp = false;
            bool saw_fat16 = false;
            bool saw_fat32 = false;

            TEST_ASSERT_EQUAL_INT(8, count);
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
                else if (strcmp(entries[i].name, "proc") == 0)
                {
                    saw_proc = true;
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
            TEST_ASSERT_TRUE(saw_proc);
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
                    bool saw_proc = false;
                    bool saw_tmp = false;
                    bool saw_fat16 = false;
                    bool saw_fat32 = false;

                    TEST_ASSERT_EQUAL_INT(8, count);
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
                        else if (strcmp(entries[i].name, "proc") == 0)
                        {
                            saw_proc = true;
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
                    TEST_ASSERT_TRUE(saw_proc);
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

    FilesystemPtr mnt_filesystem = Filesystem__heap_new(bstring("mnt/fs1"), MemFilesystemIO__new());
    MemFilesystemIOState__heap_new(mnt_filesystem);
    Filesystem__mount(root, bstring("mnt/fs1"), mnt_filesystem);

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
    KernelPtr procfs_kernel = init_test_kernel_without_filesystem();
    procfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr built_root = Filesystem__build_root_and_mounts(procfs_kernel);
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
            bool saw_proc = false;
            bool saw_tmp = false;

            TEST_ASSERT_EQUAL_INT(6, count);
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
                else if (strcmp(root_entries[i].name, "proc") == 0)
                {
                    saw_proc = true;
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
            TEST_ASSERT_TRUE(saw_proc);
            TEST_ASSERT_TRUE(saw_tmp);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_build_root_and_mounts_lists_tmp_mount_parent_when_backing_root_is_mounted(void)
{
    KernelPtr procfs_kernel = init_test_kernel_without_filesystem();
    procfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr built_root = Filesystem__build_root_and_mounts(procfs_kernel);
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
    DirectoryEntry entries[7];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/sbin", (Anything) entries, 7, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(7, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("services", entries[5].name);
            TEST_ASSERT_EQUAL_STRING("drivers", entries[6].name);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_list_dir_marks_services_as_hund_owned(void)
{
    DirectoryEntry entries[1];

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, (Anything) "/sbin/services", (Anything) entries, 1, 0
    ))
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("humpen", entries[0].name);
            TEST_ASSERT_EQUAL(PROCESS_USER_HUND, entries[0].user_id);
            TEST_ASSERT_EQUAL(PROCESS_GROUP_HUND, entries[0].group_id);
            TEST_ASSERT_EQUAL(
                IO_MODE_TYPE_FILE | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC,
                entries[0].mode
            );
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

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
#include "../../../../../../test/kernel/fixtures/memfs/memfs.h"
#include "../../../../../../kernel/subsystems/io/file/register.h"
#include "../../../../../../kernel/subsystems/io/stream/stream.h"
#include "../../../../../../kernel/system_info.h"
#include "../../../../fixtures/io_subsystem.h"
#include "../../../../fixtures/process.h"
#include "../../../../fixtures/kernel.h"

static FilesystemPtr root;
static IOStatePtr root_state;

static KernelPtr kernel;
static ProcessPtr dummy_proc;

#define IO__handler(kernel, proc, ...) IO__handler((kernel), &(proc), __VA_ARGS__)

#define TEST_IO_PATH_REQUEST(raw_path, request_flags)                                                                  \
    (Anything) & (IOPathRequest)                                                                                       \
    {                                                                                                                  \
        .path = bstr(raw_path), .flags = request_flags,                                                                \
    }

#define TEST_IO_LIST_DIR_REQUEST(raw_path, target_entries, target_max_entries)                                         \
    (Anything) & (IOListDirRequest)                                                                                    \
    {                                                                                                                  \
        .path = bstr(raw_path), .entries = target_entries, .max_entries = target_max_entries,                          \
    }

static KernelContext kernel_context = EMPTY_KERNEL_CONTEXT;
static SystemInfo fake_system_info;

static SystemInfo TestSystemInfo__new(void)
{
    SystemInfo info = SystemInfo__new();
    info.cpu_count = 4;
    info.physical_memory.physical_start = 0;
    info.physical_memory.physical_end = 0x100000000;
    info.kernel_heap.virtual_start = 0xfffff00000000000;
    info.kernel_heap.physical_start = 0x90200000;
    info.kernel_heap.physical_end = 0x98200000;
    info.uart.physical_base = 0x12345000;
    info.uart.virtual_base = 0xffff000000000000;
    info.uart.irq = 99;
    info.interrupt_controller.kind = INTERRUPT_CONTROLLER_GIC_V3;
    info.interrupt_controller.distributor_physical_base = 0x08000000;
    info.interrupt_controller.redistributor_physical_base = 0x080a0000;
    info.pci = Some(PciInfo, PciInfo__new(0x4010000000, 0x10000000, 0, 255));
    info.sd_host = Some(SdHostInfo, SdHostInfo__new(0xfe340000, 0x100, 126));
    return info;
}

static BString async_list_pending = bstr("Async list pending");
static BString async_invalid_path = bstr("Invalid path");
static BString root_memfs_type = bstr("root-memfs");
static BString async_root_type = bstr("async-root");
static BString mnt_fs1_type = bstr("mnt/fs1");
static BString ext2user_type = bstr("ext2user");
static BString root_mount_path = bstr("/");

static Result(FilesystemNodes)
    test_list_nodes(FilesystemPtr fs, IOStatePtr state, FilesystemNodePtr parent, BString path)
{
    return fs->file_io.list_nodes(state, parent, &path);
}

static Result(DirectoryEntryCount)
    test_list_dir(FilesystemPtr fs, IOStatePtr state, BString path, DirectoryEntryPtr entries, size_t max_entries)
{
    return fs->file_io.list_dir(state, &path, entries, max_entries);
}

static Result(MountResult) test_mount(FilesystemPtr fs, BString path, FilesystemPtr mounted_filesystem)
{
    return Filesystem__mount(fs, &path, mounted_filesystem);
}

static Result(Anything) test_map_result(KernelResultMapper mapper, Result(Anything) result, Anything context)
{
    return mapper(&result, context);
}

static FileIO AsyncListDirFilesystemIO__new(void);

static FilesystemPtr test_mem_filesystem(ConstBStringPtr name)
{
    FileIO file_io = MemFilesystemIO__new();
    return Filesystem__heap_new(name, &file_io);
}

static FilesystemPtr test_async_filesystem(void)
{
    FileIO file_io = AsyncListDirFilesystemIO__new();
    return Filesystem__heap_new(&async_root_type, &file_io);
}

typedef struct AsyncListDirFilesystemIOState
{
    IOState state;
    bool captured_context;
    KernelContext continuation_context;
} AsyncListDirFilesystemIOState;

typedef AsyncListDirFilesystemIOState * AsyncListDirFilesystemIOStatePtr;

static Result(DirectoryEntryCount) AsyncListDirFilesystemIO__list_dir(IOStatePtr self, ConstBStringPtr path,
                                                                      DirectoryEntryPtr entries, size_t max_entries)
{
    AsyncListDirFilesystemIOStatePtr state = ptr(self, AsyncListDirFilesystemIOState);
    match_optional(
        BlockDevice__current_context(),
        some(current_context)
        {
            state->captured_context = true;
            state->continuation_context = *current_context;
        },
        none() { state->captured_context = false; }
    );

    return Error(DirectoryEntryCount, async_list_pending, BLOCK_DEVICE_REQUEST_PENDING);
}

static FileIO AsyncListDirFilesystemIO__new(void)
{
    return (FileIO){
        .base_io = IO__new(IO__unsupported_open, IO__unsupported_read, IO__unsupported_write, IO__unsupported_accept,
                           IO__unsupported_close, IO__unsupported_park, IO__unsupported_unpark,
                           IO__unsupported_server_buffer, IO__unsupported_server_write_buffer,
                           IO__unsupported_data_available, IO__unsupported_data_consumed, IO__unsupported_eof),
        .list_nodes = FileIO__unsupported_list_nodes,
        .list_dir = AsyncListDirFilesystemIO__list_dir,
        .mkdir = FileIO__unsupported_mkdir,
        .remove = FileIO__unsupported_remove,
        .link = FileIO__unsupported_link,
    };
}

static AsyncListDirFilesystemIOStatePtr AsyncListDirFilesystemIOState__heap_new(FilesystemPtr filesystem)
{
    AsyncListDirFilesystemIOStatePtr state =
        ptr(Kernel__alloc_kernel(sizeof(AsyncListDirFilesystemIOState)), AsyncListDirFilesystemIOState);
    *state = (AsyncListDirFilesystemIOState){
        .state = {0},
        .captured_context = false,
        .continuation_context = EMPTY_KERNEL_CONTEXT,
    };
    filesystem->state = ptr(state, FilesystemIOState);
    return state;
}

void setUp(void)
{
    BlockDeviceRegistry__reset();
    fake_system_info = TestSystemInfo__new();
    dummy_proc = dummy_process(100, 11, PROC_STATE_RUNNING);

    kernel = init_test_kernel();
    root = unwrap_optional(kernel->io_subsystem->root_filesystem, RootFilesystem__heap_new());

    kernel->system_info = Some(SystemInfo, fake_system_info);
    root_state = &root->state->state;

    kernel_context = KernelContext__new(&Some(ProcessID, dummy_proc->pid), &Some(SchedulerPtr, kernel->scheduler));
}

void tearDown(void)
{
}

static BString embedded = bstr("sbin");
static BString dev = bstr("dev");
static BString boot = bstr("boot");
static BString tmp = bstr("tmp");
static BString memfs = bstr("memfs");
static BString hello_world = bstr("hello_world");
static BString corrupt = bstr("corrupt");

static const uint64_t HELLO_WORLD_SIZE = 66840;

static bool FilesystemNodes__has_name(FilesystemNodes * self, BString name)
{
    for_each_fa_ptr(FilesystemNodes, self, index, node, {
        if (BString__equals(&name, &node->name))
        {
            return true;
        }
    });

    return false;
}

IOContextPtr io_context(OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) ext_buffer)
{
    return IOContext__heap_new(&kernel_context, &Some(IOSubsystemPtr, kernel->io_subsystem), handle, ext_buffer);
}

IOContextPtr open_context()
{
    return io_context(&None(IOHandle), &None(ExternalBuffer));
}

IOContextPtr close_context(IOHandle handle)
{
    return io_context(&Some(IOHandle, handle), &None(ExternalBuffer));
}

IOContextPtr rw_context(IOHandle handle, DataPtr data, size_t size)
{
    return io_context(&Some(IOHandle, handle), &Some(ExternalBuffer, ExternalBuffer__new(data, size)));
}

static BString numbered_memfs_path(int index)
{
    Char path[] = "/tmp/memfs/file00";
    path[15] = '0' + (index / 10);
    path[16] = '0' + (index % 10);
    return BString__new((char *)path);
}

void test_it_can_list_files_from_root(void)
{
    Result(FilesystemNodes) nodes = test_list_nodes(root, root_state, root->root, bstring("/"));

    match_result(
        nodes,
        success(result)
        {
            TEST_ASSERT_EQUAL_INT(3, FilesystemNodes__len(&result));
            FilesystemNodePtr node = unwrap_optional(FilesystemNodes__at(&result, 0), root->root);
            TEST_ASSERT_EQUAL_BSTRING(embedded, node->name);
            FilesystemNodePtr boot_node = unwrap_optional(FilesystemNodes__at(&result, 1), root->root);
            TEST_ASSERT_EQUAL_BSTRING(boot, boot_node->name);
            FilesystemNodePtr tmp_node = unwrap_optional(FilesystemNodes__at(&result, 2), root->root);
            TEST_ASSERT_EQUAL_BSTRING(tmp, tmp_node->name);
        },
        error_test_fail()
    );
}

void test_block_registration_publishes_device_metadata_and_alias(void)
{
    ProcessPtr service = dummy_process(200, 11, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    match_result(
        BlockDeviceRegistry__register(&bstr("/dev/virtio1"), service, 128, kernel->message_broker),
        success(code) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_OK, code); },
        error_test_fail()
    );

    match_result(
        DeviceRegistry__find(k_v_ptr(kernel->device_registry.mapping, DeviceRegistry), &bstr("/dev/hd0")),
        success(found)
        {
            match_optional(
                found,
                some(record)
                {
                    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/virtio1"), record.path);
                    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/virtio1"), record.service);
                    TEST_ASSERT_EQUAL(512, record.block_size);
                    TEST_ASSERT_EQUAL(128, record.block_count);
                    TEST_ASSERT_EQUAL(service->pid, record.owner_pid);
                },
                none() { TEST_FAIL(); }
            );
        },
        error_test_fail()
    );
}

void test_kernel_no_longer_serves_devfs_metadata(void)
{
    ProcessPtr service = dummy_process(200, 11, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;

    match_result(
        BlockDeviceRegistry__register(&bstr("/dev/virtio1"), service, 128, kernel->message_broker),
        success(code) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_OK, code); },
        error_test_fail()
    );

    Result(IOHandle) metadata_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/dev/hd0"), IO_OPEN_READ);

    match_result(
        metadata_handle,
        success(handle) { TEST_FAIL_MESSAGE("Kernel still serves device metadata"); },
        error(code, message) { TEST_ASSERT_NOT_EQUAL(IO_OK, code); }
    );
}

void test_boot_sysinfo_coexists_with_nested_config_mount_and_proc_is_absent(void)
{
    KernelPtr boot_kernel = init_test_kernel_without_filesystem();
    boot_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr built_root = Filesystem__build_root_and_mounts(boot_kernel);
    FilesystemPtr config = test_mem_filesystem(&bstr("config"));
    MemFilesystemIOState__heap_new(config);
    test_mount(built_root, bstring("/boot/config"), config);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(built_root);
    match_result(
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/boot/sysinfo"), IO_OPEN_READ),
        success(handle) { TEST_ASSERT_GREATER_OR_EQUAL(0, handle); },
        error_test_fail()
    );
    match_result(
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/boot/config/fstab"), IO_OPEN_WRITE),
        success(handle) { TEST_ASSERT_GREATER_OR_EQUAL(0, handle); },
        error_test_fail()
    );
    match_result(
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/proc/sysinfo"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_INVALID_PATH, code); }
    );
}

void test_bootfs_can_open_and_read_system_info(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    bootfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    SystemInfoPtr system_info = &bootfs_kernel->system_info.some.val;
    for (uint64_t index = 0; index < 12; index++)
    {
        PhysicalMemoryRegion region =
            PhysicalMemoryRegion__new(0x40000000 + (index * 0x1000000), 0x40000000 + ((index + 1) * 0x1000000));
        PhysicalMemoryRegions__push(&system_info->physical_memory.free_regions, region);
    }

    FilesystemPtr bootfs_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(bootfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/boot/sysinfo"), IO_OPEN_READ);

    match_result(
        sysinfo_handle,
        success(handle)
        {
            Char buffer[4096];
            ByteCount total = 0;
            bool eof = false;
            while (!eof && total < sizeof(buffer) - 1)
            {
                match_result(
                    IOSubsystem__read(mounted_kernel->io_subsystem, rw_context(handle, (DataPtr)&buffer[total], 128)),
                    success(bytes) { total += bytes; },
                    error(code, message)
                    {
                        if (code == IO_EOF)
                        {
                            eof = true;
                        }
                        else
                        {
                            TEST_FAIL();
                        }
                    }
                );
            }
            buffer[total] = '\0';

            TEST_ASSERT_TRUE(eof);
            TEST_ASSERT_GREATER_THAN(1024, total);
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "cpu.count=4\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.physical_start=0x0\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.physical_end=0x100000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.physical_size=0x100000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.physical_size_human=4 GiB\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.free_region.count=12\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.free_region.11.physical_start=0x4b000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.free_region.11.physical_end=0x4c000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.free_region.11.physical_size_human=16 MiB\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.kernel_heap.physical_start=0x90200000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.kernel_heap.physical_end=0x98200000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.kernel_heap.physical_size=0x8000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.kernel_heap.physical_size_human=128 MiB\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "uart.physical_base=0x12345000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "uart.irq=99\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "uart.mmio_size=0x1000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.present=0\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "pci.present=1\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "pci.ecam.physical_base=0x4010000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "pci.ecam.size=0x10000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "pci.ecam.size_human=256 MiB\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "pci.bus.start=0\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "pci.bus.end=255\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "sd_host.present=1\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "sd_host.physical_base=0xfe340000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "sd_host.mmio_size=0x100\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "sd_host.irq=126\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "interrupt_controller.kind=2\n"));
            TEST_ASSERT_NOT_NULL(strstr(buffer, "interrupt_controller.distributor_physical_base=0x8000000\n"));
        },
        error_test_fail()
    );
}

void test_bootfs_can_read_system_info_from_non_zero_offset(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    bootfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    SystemInfoPtr system_info = &bootfs_kernel->system_info.some.val;
    for (uint64_t index = 0; index < 12; index++)
    {
        PhysicalMemoryRegion region =
            PhysicalMemoryRegion__new(0x40000000 + (index * 0x1000000), 0x40000000 + ((index + 1) * 0x1000000));
        PhysicalMemoryRegions__push(&system_info->physical_memory.free_regions, region);
    }

    FilesystemPtr bootfs_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(bootfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/boot/sysinfo"), IO_OPEN_READ);

    match_result(
        sysinfo_handle,
        success(handle)
        {
            Char skipped[128];
            match_result(
                IOSubsystem__read(mounted_kernel->io_subsystem, rw_context(handle, (DataPtr)skipped, sizeof(skipped))),
                success(bytes) { TEST_ASSERT_EQUAL_UINT64(128, bytes); },
                error_test_fail()
            );

            Char buffer[4096];
            ByteCount total = 0;
            bool eof = false;
            while (!eof && total < sizeof(buffer) - 1)
            {
                match_result(
                    IOSubsystem__read(mounted_kernel->io_subsystem, rw_context(handle, (DataPtr)&buffer[total], 128)),
                    success(bytes) { total += bytes; },
                    error(code, message)
                    {
                        if (code == IO_EOF)
                        {
                            eof = true;
                        }
                        else
                        {
                            TEST_FAIL();
                        }
                    }
                );
            }
            buffer[total] = '\0';

            TEST_ASSERT_TRUE(eof);
            TEST_ASSERT_GREATER_THAN(896, total);
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "memory.free_region.11.physical_end=0x4c000000\n"));
            TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "interrupt_controller.kind=2\n"));
        },
        error_test_fail()
    );
}

void test_bootfs_can_read_framebuffer_system_info(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    SystemInfo framebuffer_system_info = fake_system_info;
    framebuffer_system_info.framebuffer =
        Some(FramebufferInfo, FramebufferInfo__new(0x3e000000, 0x7e9000, 1920, 1080, 2048,
                                                   FRAMEBUFFER_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR, 0x00ff0000,
                                                   0x0000ff00, 0x000000ff, 0xff000000));
    bootfs_kernel->system_info = Some(SystemInfo, framebuffer_system_info);

    FilesystemPtr bootfs_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(bootfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/boot/sysinfo"), IO_OPEN_READ);

    match_result(
        sysinfo_handle,
        success(handle)
        {
            Char buffer[2048];
            match_result(
                IOSubsystem__read(mounted_kernel->io_subsystem,
                                  rw_context(handle, (DataPtr)buffer, sizeof(buffer) - 1)),
                success(bytes)
                {
                    buffer[bytes] = '\0';

                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.present=1\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.physical_base=0x3e000000\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.physical_size=0x7e9000\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.physical_size_human=8100 KiB\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.width=1920\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.height=1080\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.pixels_per_scanline=2048\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.pixel_format=1\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.red_mask=0xff0000\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.green_mask=0xff00\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.blue_mask=0xff\n"));
                    TEST_ASSERT_NOT_NULL(strstr((char *)buffer, "framebuffer.reserved_mask=0xff000000\n"));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_bootfs_returns_eof_after_streaming_system_info(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    bootfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr bootfs_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(bootfs_root);

    Result(IOHandle) sysinfo_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, open_context(), &bstr("/boot/sysinfo"), IO_OPEN_READ);

    match_result(
        sysinfo_handle,
        success(handle)
        {
            Char buffer[2048];
            match_result(
                IOSubsystem__read(mounted_kernel->io_subsystem, rw_context(handle, (DataPtr)buffer, sizeof(buffer))),
                success_do_nothing(),
                error_test_fail()
            );

            match_result(
                IOSubsystem__read(mounted_kernel->io_subsystem, rw_context(handle, (DataPtr)buffer, sizeof(buffer))),
                success_test_fail(),
                error(code, message) { TEST_ASSERT_EQUAL_INT64(IO_EOF, code); }
            );
        },
        error_test_fail()
    );
}

void test_it_can_list_files_to_subdir(void)
{
    Result(FilesystemNodes) nodes = test_list_nodes(root, root_state, root->root, bstring("/sbin/"));

    match_result(
        nodes,
        success(result)
        {
            TEST_ASSERT_EQUAL_INT(4, FilesystemNodes__len(&result));
            FilesystemNodePtr node = unwrap_optional(FilesystemNodes__at(&result, 0), root->root);
            TEST_ASSERT_EQUAL_BSTRING(hello_world, node->name);
        },
        error_test_fail()
    );
}

void test_it_can_list_nested_mount_parent(void)
{
    Result(FilesystemNodes) nodes = test_list_nodes(root, root_state, root->root, bstring("/tmp"));

    match_result(
        nodes,
        success(result)
        {
            TEST_ASSERT_EQUAL_INT(1, FilesystemNodes__len(&result));
            FilesystemNodePtr node = unwrap_optional(FilesystemNodes__at(&result, 0), root->root);
            TEST_ASSERT_EQUAL_BSTRING(memfs, node->name);
        },
        error_test_fail()
    );
}

void test_filesystem_table_adds_mount_entries(void)
{
    FilesystemTable table = FilesystemTable__new();
    BString device_path = bstring("/dev/virtio1");
    BString filesystem_type = bstring("userspacefs");
    BString mountpoint = bstring("/mnt/fs1");

    match_result(
        FilesystemTable__add(&table, &device_path, &filesystem_type, &mountpoint),
        success(index) { TEST_ASSERT_EQUAL(0, index); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, FilesystemTableEntries__len(&table.entries));
    match_optional(
        FilesystemTableEntries__ptr_at(&table.entries, 0),
        some(entry)
        {
            TEST_ASSERT_EQUAL_BSTRING(entry->device_path, device_path);
            TEST_ASSERT_EQUAL_BSTRING(entry->filesystem_type, filesystem_type);
            TEST_ASSERT_EQUAL_BSTRING(entry->mountpoint, mountpoint);
            TEST_ASSERT_FALSE(entry->match_block_count);
            TEST_ASSERT_EQUAL_UINT64(0, entry->block_count);
        },
        none_test_fail()
    );
}

void test_boot_mount_table_keeps_config_but_not_root_disk(void)
{
    KernelPtr boot_kernel = init_test_kernel();
    FilesystemPtr filesystem = Filesystem__build_root_and_mounts(boot_kernel);
    TEST_ASSERT_EQUAL(1, FilesystemTableEntries__len(&filesystem->filesystem_table.entries));
    match_optional(
        FilesystemTableEntries__ptr_at(&filesystem->filesystem_table.entries, 0),
        some(entry)
        {
            TEST_ASSERT_EQUAL_BSTRING(entry->mountpoint, bstr("/boot/config"));
            TEST_ASSERT_TRUE(entry->match_block_count);
        },
        none_test_fail()
    );
}

void test_filesystem_table_adds_block_count_mount_entries(void)
{
    FilesystemTable table = FilesystemTable__new();
    uint64_t block_count = 16384;
    BString filesystem_type = bstring("ext2");
    BString mountpoint = bstring("/boot/config");

    match_result(
        FilesystemTable__add_by_block_count(&table, block_count, &filesystem_type, &mountpoint),
        success(index) { TEST_ASSERT_EQUAL(0, index); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, FilesystemTableEntries__len(&table.entries));
    match_optional(
        FilesystemTableEntries__ptr_at(&table.entries, 0),
        some(entry)
        {
            TEST_ASSERT_EQUAL_BSTRING(entry->device_path, empty_bstr());
            TEST_ASSERT_EQUAL_BSTRING(entry->filesystem_type, filesystem_type);
            TEST_ASSERT_EQUAL_BSTRING(entry->mountpoint, mountpoint);
            TEST_ASSERT_TRUE(entry->match_block_count);
            TEST_ASSERT_EQUAL_UINT64(block_count, entry->block_count);
        },
        none_test_fail()
    );
}

void test_it_can_open_files_and_read_from_embedded_via_io_subsystem(void)
{
    Result(IOHandle) handle = IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/sbin/hello_world"), 0);

    match_result(
        handle,
        success(hand)
        {
            Char buffer[_128KB];

            Result(ByteCount) res = IOSubsystem__read(kernel->io_subsystem, rw_context(hand, buffer, sizeof(buffer)));

            match_result(res,
                success(bytes) { TEST_ASSERT_EQUAL(HELLO_WORLD_SIZE, bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_io_list_dir_lists_root_mounts(void)
{
    DirectoryEntry entries[4];

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, TEST_IO_LIST_DIR_REQUEST("/", entries, 4), 0, 0,
                    0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(3, count);
            TEST_ASSERT_EQUAL_STRING("sbin", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("boot", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("tmp", entries[2].name);
        },
        error_test_fail()
    );
}

void test_io_list_dir_lists_backing_root_entries_and_mount_overlays(void)
{
    FilesystemPtr backing_root = test_mem_filesystem(&root_memfs_type);
    MemFilesystemIOState__heap_new(backing_root);
    test_mount(root, bstring("/"), backing_root);

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/docs", 0), 0, 0, 0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/hello.txt"), IO_OPEN_WRITE);

    match_result(
        write_handle,
        success(write_hand)
        {
            Char data[] = "root";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, 4)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(4, bytes); },
                error_test_fail()
            );

            match_result(
                IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)),
                success(handle) { TEST_ASSERT_EQUAL(write_hand, handle); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    DirectoryEntry entries[6] = {0};
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, TEST_IO_LIST_DIR_REQUEST("/", entries, 6), 0, 0,
                    0),
        success(count)
        {
            bool saw_docs = false;
            bool saw_hello = false;
            bool saw_ebin = false;
            bool saw_dev = false;
            bool saw_boot = false;
            bool saw_tmp = false;
            TEST_ASSERT_EQUAL_INT(5, count);
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
                else if (strcmp(entries[i].name, "boot") == 0)
                {
                    saw_boot = true;
                }
                else if (strcmp(entries[i].name, "tmp") == 0)
                {
                    saw_tmp = true;
                }
            }

            TEST_ASSERT_TRUE(saw_docs);
            TEST_ASSERT_TRUE(saw_hello);
            TEST_ASSERT_TRUE(saw_ebin);
            TEST_ASSERT_FALSE(saw_dev);
            TEST_ASSERT_TRUE(saw_boot);
            TEST_ASSERT_TRUE(saw_tmp);
        },
        error_test_fail()
    );
}

void test_root_async_list_dir_merges_mount_overlays_on_completion(void)
{
    FilesystemPtr async_root = test_async_filesystem();
    AsyncListDirFilesystemIOStatePtr async_state = AsyncListDirFilesystemIOState__heap_new(async_root);
    test_mount(root, bstring("/"), async_root);

    DirectoryEntry entries[6] = {0};

    BlockDevice__set_current_context(&kernel_context);
    match_result(
        test_list_dir(root, root_state, bstring("/"), entries, 6),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_TRUE(async_state->captured_context);
    TEST_ASSERT_TRUE(async_state->continuation_context.result_mapper.present);

    entries[0] = DirectoryEntry__new(0, 0, 0, 0, &bstr("docs"));
    entries[1] = DirectoryEntry__new(0, 0, 0, 0, &bstr("hello.txt"));

    match_optional(
        async_state->continuation_context.result_mapper,
        some(mapper)
        {
            match_result(
                test_map_result(mapper, Success(Anything, (Anything)2),
                                async_state->continuation_context.result_mapper_context),
                success(value)
                {
                    DirectoryEntryCount count = (DirectoryEntryCount)value;
                    bool saw_docs = false;
                    bool saw_hello = false;
                    bool saw_ebin = false;
                    bool saw_dev = false;
                    bool saw_boot = false;
                    bool saw_tmp = false;
                    TEST_ASSERT_EQUAL_INT(5, count);
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
                        else if (strcmp(entries[i].name, "boot") == 0)
                        {
                            saw_boot = true;
                        }
                        else if (strcmp(entries[i].name, "tmp") == 0)
                        {
                            saw_tmp = true;
                        }
                    }

                    TEST_ASSERT_TRUE(saw_docs);
                    TEST_ASSERT_TRUE(saw_hello);
                    TEST_ASSERT_TRUE(saw_ebin);
                    TEST_ASSERT_FALSE(saw_dev);
                    TEST_ASSERT_TRUE(saw_boot);
                    TEST_ASSERT_TRUE(saw_tmp);
                },
                error_test_fail()
            );
        },
        none_test_fail()
    );
}

void test_root_async_list_dir_synthesizes_mount_parent_on_backing_invalid_path(void)
{
    FilesystemPtr async_root = test_async_filesystem();
    AsyncListDirFilesystemIOStatePtr async_state = AsyncListDirFilesystemIOState__heap_new(async_root);
    test_mount(root, bstring("/"), async_root);

    DirectoryEntry tmp_entries[4] = {0};
    DirectoryEntry mnt_entries[4] = {0};

    BlockDevice__set_current_context(&kernel_context);
    match_result(
        test_list_dir(root, root_state, bstring("/tmp"), tmp_entries, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_TRUE(async_state->captured_context);
    TEST_ASSERT_TRUE(async_state->continuation_context.result_mapper.present);

    match_optional(
        async_state->continuation_context.result_mapper,
        some(tmp_mapper)
        {
            match_result(
                test_map_result(tmp_mapper, Error(Anything, async_invalid_path, IO_INVALID_PATH),
                                async_state->continuation_context.result_mapper_context),
                success(value)
                {
                    DirectoryEntryCount count = (DirectoryEntryCount)value;
                    TEST_ASSERT_EQUAL_INT(1, count);
                    TEST_ASSERT_EQUAL_STRING("memfs", tmp_entries[0].name);
                },
                error_test_fail()
            );
        },
        none_test_fail()
    );

    FilesystemPtr mnt_filesystem = test_mem_filesystem(&mnt_fs1_type);
    MemFilesystemIOState__heap_new(mnt_filesystem);
    test_mount(root, bstring("mnt/fs1"), mnt_filesystem);

    BlockDevice__set_current_context(&kernel_context);
    match_result(
        test_list_dir(root, root_state, bstring("/mnt"), mnt_entries, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_TRUE(async_state->captured_context);
    TEST_ASSERT_TRUE(async_state->continuation_context.result_mapper.present);

    match_optional(
        async_state->continuation_context.result_mapper,
        some(mnt_mapper)
        {
            match_result(
                test_map_result(mnt_mapper, Error(Anything, async_invalid_path, IO_INVALID_PATH),
                                async_state->continuation_context.result_mapper_context),
                success(value)
                {
                    DirectoryEntryCount count = (DirectoryEntryCount)value;
                    TEST_ASSERT_EQUAL_INT(1, count);
                    TEST_ASSERT_EQUAL_STRING("fs1", mnt_entries[0].name);
                },
                error_test_fail()
            );
        },
        none_test_fail()
    );
}

void test_build_root_and_mounts_lists_backing_root_entries_and_mount_overlays(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    bootfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr built_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    FilesystemPtr backing_root = test_mem_filesystem(&root_memfs_type);
    MemFilesystemIOState__heap_new(backing_root);
    test_mount(built_root, bstring("/"), backing_root);

    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(built_root);
    KernelContext mounted_context =
        KernelContext__new(&Some(ProcessID, dummy_proc->pid), &Some(SchedulerPtr, mounted_kernel->scheduler));
    IOContextPtr mounted_open_context = IOContext__heap_new(
        &mounted_context, &Some(IOSubsystemPtr, mounted_kernel->io_subsystem), &None(IOHandle), &None(ExternalBuffer));

    match_result(
        IO__handler(mounted_kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/docs", 0), 0, 0, 0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    Result(IOHandle) write_handle =
        IOSubsystem__open(mounted_kernel->io_subsystem, mounted_open_context, &bstr("/hello.txt"), IO_OPEN_WRITE);

    match_result(
        write_handle,
        success(write_hand)
        {
            ExternalBuffer ext_buffer = ExternalBuffer__new((DataPtr) "root", 4);
            IOContextPtr mounted_rw_context =
                IOContext__heap_new(&mounted_context, &Some(IOSubsystemPtr, mounted_kernel->io_subsystem),
                                    &Some(IOHandle, write_hand), &Some(ExternalBuffer, ext_buffer));
            IOContextPtr mounted_close_context =
                IOContext__heap_new(&mounted_context, &Some(IOSubsystemPtr, mounted_kernel->io_subsystem),
                                    &Some(IOHandle, write_hand), &None(ExternalBuffer));

            match_result(
                IOSubsystem__write(mounted_kernel->io_subsystem, mounted_rw_context),
                success(bytes) { TEST_ASSERT_EQUAL_INT(4, bytes); },
                error_test_fail()
            );

            match_result(
                IOSubsystem__close(mounted_kernel->io_subsystem, mounted_close_context),
                success(handle) { TEST_ASSERT_EQUAL(write_hand, handle); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    DirectoryEntry root_entries[8] = {0};
    match_result(
        IO__handler(mounted_kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/", root_entries, 8), 0, 0, 0),
        success(count)
        {
            bool saw_docs = false;
            bool saw_hello = false;
            bool saw_ebin = false;
            bool saw_dev = false;
            bool saw_boot = false;
            bool saw_tmp = false;

            TEST_ASSERT_EQUAL_INT(3, count);
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
                else if (strcmp(root_entries[i].name, "boot") == 0)
                {
                    saw_boot = true;
                }
                else if (strcmp(root_entries[i].name, "tmp") == 0)
                {
                    saw_tmp = true;
                }
            }

            TEST_ASSERT_TRUE(saw_docs);
            TEST_ASSERT_TRUE(saw_hello);
            TEST_ASSERT_FALSE(saw_ebin);
            TEST_ASSERT_FALSE(saw_dev);
            TEST_ASSERT_TRUE(saw_boot);
            TEST_ASSERT_FALSE(saw_tmp);
        },
        error_test_fail()
    );
}

void test_build_root_and_mounts_does_not_mount_kernel_memfs(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    bootfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr built_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    FilesystemPtr backing_root = test_mem_filesystem(&root_memfs_type);
    MemFilesystemIOState__heap_new(backing_root);
    test_mount(built_root, bstring("/"), backing_root);

    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(built_root);

    DirectoryEntry tmp_entries[4] = {0};
    match_result(
        IO__handler(mounted_kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp", tmp_entries, 4), 0, 0, 0),
        success(count) { TEST_FAIL_MESSAGE("Kernel must not provide /tmp"); },
        error(code, message) { TEST_ASSERT_EQUAL(IO_INVALID_PATH, code); }
    );
}

void test_build_root_and_mounts_lists_mnt_mount_parent_when_backing_root_is_mounted(void)
{
    KernelPtr bootfs_kernel = init_test_kernel_without_filesystem();
    bootfs_kernel->system_info = Some(SystemInfo, fake_system_info);
    FilesystemPtr built_root = Filesystem__build_root_and_mounts(bootfs_kernel);
    FilesystemPtr backing_root = test_mem_filesystem(&root_memfs_type);
    FilesystemPtr ext2user = test_mem_filesystem(&ext2user_type);
    MemFilesystemIOState__heap_new(backing_root);
    MemFilesystemIOState__heap_new(ext2user);
    test_mount(built_root, bstring("/"), backing_root);
    test_mount(built_root, bstring("/mnt/ext2user"), ext2user);

    KernelPtr mounted_kernel = init_test_kernel_with_filesystem(built_root);

    DirectoryEntry mnt_entries[4] = {0};
    match_result(
        IO__handler(mounted_kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/mnt", mnt_entries, 4), 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("ext2user", mnt_entries[0].name);
        },
        error_test_fail()
    );
}

void test_io_list_dir_lists_ebin_files(void)
{
    DirectoryEntry entries[4];

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, TEST_IO_LIST_DIR_REQUEST("/sbin", entries, 4), 0,
                    0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(4, count);
            TEST_ASSERT_EQUAL_STRING("hello_world", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("services", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("shell", entries[2].name);
            TEST_ASSERT_EQUAL_STRING("drivers", entries[3].name);
        },
        error_test_fail()
    );
}

void test_io_list_dir_marks_services_as_hund_owned(void)
{
    DirectoryEntry entries[4];

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/sbin/services", entries, 4), 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(4, count);
            TEST_ASSERT_EQUAL_STRING("idle", entries[0].name);
            TEST_ASSERT_EQUAL_STRING("hundeschule", entries[1].name);
            TEST_ASSERT_EQUAL_STRING("hundeschau", entries[2].name);
            TEST_ASSERT_EQUAL_STRING("humpen", entries[3].name);
            for (DirectoryEntryCount index = 0; index < count; index++)
            {
                TEST_ASSERT_EQUAL(PROCESS_USER_HUND, entries[index].user_id);
                TEST_ASSERT_EQUAL(PROCESS_GROUP_HUND, entries[index].group_id);
                TEST_ASSERT_EQUAL(IO_MODE_TYPE_FILE | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC | IO_MODE_GROUP_READ |
                                      IO_MODE_GROUP_EXEC,
                                  entries[index].mode);
            }
        },
        error_test_fail()
    );
}

void test_io_list_dir_lists_tmp_mounts(void)
{
    DirectoryEntry entries[4];

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, TEST_IO_LIST_DIR_REQUEST("/tmp", entries, 4), 0,
                    0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("memfs", entries[0].name);
        },
        error_test_fail()
    );
}

void test_io_list_dir_lists_empty_memfs(void)
{
    DirectoryEntry entries[4];

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs", entries, 4), 0, 0, 0),
        success(count) { TEST_ASSERT_EQUAL_INT(0, count); },
        error_test_fail()
    );
}

void test_memfs_write_read_and_list_file(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/notes"), IO_OPEN_WRITE);

    match_result(
        write_handle,
        success(write_hand)
        {
            Char data[] = "hello memfs";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, 11)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(11, bytes); },
                error_test_fail()
            );

            match_result(
                IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)),
                success(handle) { TEST_ASSERT_EQUAL(write_hand, handle); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    DirectoryEntry entries[4];
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs", entries, 4), 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("notes", entries[0].name);
        },
        error_test_fail()
    );

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/notes"), IO_OPEN_READ);

    match_result(
        read_handle,
        success(read_hand)
        {
            Char buffer[32];
            match_result(
                IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr)buffer, 11)),
                success(bytes)
                {
                    TEST_ASSERT_EQUAL_INT(11, bytes);
                    buffer[11] = '\0';
                    TEST_ASSERT_EQUAL_STRING("hello memfs", (char *)buffer);
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_memfs_mkdir_lists_directory_and_allows_file_inside(void)
{
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/tmp/memfs/docs", 0), 0, 0,
                    0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry root_entries[4];
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs", root_entries, 4), 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("docs", root_entries[0].name);
        },
        error_test_fail()
    );

    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/docs/notes"), IO_OPEN_WRITE);

    match_result(
        write_handle,
        success(write_hand)
        {
            Char data[] = "nested";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data) - 1)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    DirectoryEntry docs_entries[4];
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs/docs", docs_entries, 4), 0, 0, 0),
        success(count)
        {
            TEST_ASSERT_EQUAL_INT(1, count);
            TEST_ASSERT_EQUAL_STRING("notes", docs_entries[0].name);
        },
        error_test_fail()
    );
}

void test_memfs_rm_removes_file_from_directory(void)
{
    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/remove-me"), IO_OPEN_WRITE),
        success(write_hand)
        {
            Char data[] = "delete";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data) - 1)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, TEST_IO_PATH_REQUEST("/tmp/memfs/remove-me", 0), 0,
                    0, 0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4];
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs", entries, 4), 0, 0, 0),
        success(count) { TEST_ASSERT_EQUAL_INT(0, count); },
        error_test_fail()
    );

    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/remove-me"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code); }
    );
}

void test_memfs_rm_directory_returns_unsupported(void)
{
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/tmp/memfs/docs", 0), 0, 0,
                    0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, TEST_IO_PATH_REQUEST("/tmp/memfs/docs", 0), 0, 0,
                    0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code); }
    );
}

void test_memfs_rm_recursive_removes_directory_tree(void)
{
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/tmp/memfs/docs", 0), 0, 0,
                    0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/tmp/memfs/docs/nested", 0),
                    0, 0, 0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/docs/nested/notes"), IO_OPEN_WRITE),
        success(write_hand)
        {
            Char data[] = "nested";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data) - 1)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE,
                    TEST_IO_PATH_REQUEST("/tmp/memfs/docs", IO_REMOVE_RECURSIVE), 0, 0, 0),
        success(status) { TEST_ASSERT_EQUAL_INT(IO_OK, status); },
        error_test_fail()
    );

    DirectoryEntry entries[4];
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs", entries, 4), 0, 0, 0),
        success(count) { TEST_ASSERT_EQUAL_INT(0, count); },
        error_test_fail()
    );

    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/docs/nested/notes"), IO_OPEN_READ),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code); }
    );
}

void test_mkdir_and_rm_on_embeddedfs_return_unsupported(void)
{
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_MKDIR, TEST_IO_PATH_REQUEST("/sbin/newdir", 0), 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code); }
    );

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_REMOVE, TEST_IO_PATH_REQUEST("/sbin/hello_world", 0), 0, 0,
                    0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code); }
    );
}

void test_memfs_file_can_grow_beyond_one_page(void)
{
    Result(IOHandle) write_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/big"), IO_OPEN_WRITE);

    match_result(
        write_handle,
        success(write_hand)
        {
            Char data[PAGE_SIZE + 17];
            for (size_t i = 0; i < sizeof(data); i++)
            {
                data[i] = 'a' + (i % 26);
            }

            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data))),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data), bytes); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    Result(IOHandle) read_handle =
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/big"), IO_OPEN_READ);

    match_result(
        read_handle,
        success(read_hand)
        {
            Char buffer[PAGE_SIZE + 17];
            match_result(
                IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr)buffer, sizeof(buffer))),
                success(bytes)
                {
                    TEST_ASSERT_EQUAL_INT(sizeof(buffer), bytes);
                    for (size_t i = 0; i < sizeof(buffer); i++)
                    {
                        TEST_ASSERT_EQUAL_CHAR('a' + (i % 26), buffer[i]);
                    }
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_memfs_read_returns_error_when_file_pages_are_missing(void)
{
    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/corrupt"), IO_OPEN_WRITE),
        success(write_hand)
        {
            Char data[] = "data";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data) - 1)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes); },
                error_test_fail()
            );

            match_result(
                IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)),
                success(closed) { TEST_ASSERT_EQUAL(write_hand, closed); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_optional(
        Mounts__get(&root->mounts, bstring("tmp/memfs")),
        some(mounted_fs)
        {
            MemFilesystemIOStatePtr mem_state = (MemFilesystemIOStatePtr)mounted_fs->state;
            for_each_ll(MemFiles, mem_state->files, file, {
                if (BString__equals(&file.name, &corrupt))
                {
                    file__node->value.pages = MemFilePages__new();
                    file__node->value.capacity = PAGE_SIZE;
                    file__node->value.size = sizeof("data") - 1;
                }
            });
        },
        none_test_fail()
    );

    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/corrupt"), IO_OPEN_READ),
        success(read_hand)
        {
            Char buffer[8];
            match_result(
                IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr)buffer, sizeof(buffer))),
                success_test_fail(),
                error(code, message) { TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code); }
            );
        },
        error_test_fail()
    );
}

void test_memfs_write_open_truncates_existing_file(void)
{
    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/truncate"), IO_OPEN_WRITE),
        success(write_hand)
        {
            Char data[] = "longer value";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data) - 1)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes); },
                error_test_fail()
            );

            match_result(
                IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)),
                success(closed) { TEST_ASSERT_EQUAL(write_hand, closed); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/truncate"), IO_OPEN_WRITE),
        success(write_hand)
        {
            Char data[] = "short";
            match_result(
                IOSubsystem__write(kernel->io_subsystem, rw_context(write_hand, (DataPtr)data, sizeof(data) - 1)),
                success(bytes) { TEST_ASSERT_EQUAL_INT(sizeof(data) - 1, bytes); },
                error_test_fail()
            );

            match_result(
                IOSubsystem__close(kernel->io_subsystem, close_context(write_hand)),
                success(closed) { TEST_ASSERT_EQUAL(write_hand, closed); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    match_result(
        IOSubsystem__open(kernel->io_subsystem, open_context(), &bstr("/tmp/memfs/truncate"), IO_OPEN_READ),
        success(read_hand)
        {
            Char buffer[16];
            match_result(
                IOSubsystem__read(kernel->io_subsystem, rw_context(read_hand, (DataPtr)buffer, sizeof(buffer))),
                success(bytes)
                {
                    TEST_ASSERT_EQUAL_INT(5, bytes);
                    buffer[5] = '\0';
                    TEST_ASSERT_EQUAL_STRING("short", (char *)buffer);
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_memfs_can_create_more_than_sixty_four_files(void)
{
    for (int i = 0; i < 65; i++)
    {
        BString path = numbered_memfs_path(i);
        match_result(
            IOSubsystem__open(kernel->io_subsystem, open_context(), &path, IO_OPEN_WRITE),
            success(handle)
            {
                match_result(
                    IOSubsystem__close(kernel->io_subsystem, close_context(handle)),
                    success(closed) { TEST_ASSERT_EQUAL(handle, closed); },
                    error_test_fail()
                );
            },
            error_test_fail()
        );
    }

    DirectoryEntry entries[70];
    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR,
                    TEST_IO_LIST_DIR_REQUEST("/tmp/memfs", entries, 70), 0, 0, 0),
        success(count) { TEST_ASSERT_EQUAL_INT(65, count); },
        error_test_fail()
    );
}

void test_io_list_dir_invalid_path_returns_error(void)
{
    DirectoryEntry entries[4];

    match_result(
        IO__handler(kernel, Some(ProcessPtr, dummy_proc), IO_LIST_DIR, TEST_IO_LIST_DIR_REQUEST("/missing", entries, 4),
                    0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL_INT(IO_INVALID_PATH, code); }
    );
}

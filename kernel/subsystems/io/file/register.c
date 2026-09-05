#include "register.h"

#include "../../../kernel_core.h"
#include "../block/block_device.h"
#include "filesystem.h"
#include "devfs/devfs.h"
#include "embedded/embedded.h"
#include "memfs/memfs.h"
#include "procfs/procfs.h"

bool Fat16FilesystemIO__available() __attribute__((weak));
bool Fat16FilesystemIO__available()
{
    return false;
}

static FileIO FileIO__new_all_unsupported()
{
    return (FileIO) {
        .base_io = IO__new(
            IO__unsupported_open,
            IO__unsupported_read,
            IO__unsupported_write,
            IO__unsupported_accept,
            IO__unsupported_close,
            IO__unsupported_park,
            IO__unsupported_unpark,
            IO__unsupported_server_buffer,
            IO__unsupported_server_write_buffer,
            IO__unsupported_data_available,
            IO__unsupported_data_consumed,
            IO__unsupported_eof
        ),
        .list_nodes = FileIO__unsupported_list_nodes,
        .list_dir = FileIO__unsupported_list_dir,
        .mkdir = FileIO__unsupported_mkdir,
        .remove = FileIO__unsupported_remove,
        .link = FileIO__unsupported_link,
    };
}

FileIO Fat16FilesystemIO__new() __attribute__((weak));
FileIO Fat16FilesystemIO__new()
{
    return FileIO__new_all_unsupported();
}

void * Fat16FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors
) __attribute__((weak));
void * Fat16FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return 0;
}

bool Fat32FilesystemIO__available() __attribute__((weak));
bool Fat32FilesystemIO__available()
{
    return false;
}

FileIO Fat32FilesystemIO__new() __attribute__((weak));
FileIO Fat32FilesystemIO__new()
{
    return FileIO__new_all_unsupported();
}

void * Fat32FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors
) __attribute__((weak));
void * Fat32FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return 0;
}

bool Ext2FilesystemIO__available() __attribute__((weak));
bool Ext2FilesystemIO__available()
{
    return false;
}

FileIO Ext2FilesystemIO__new() __attribute__((weak));
FileIO Ext2FilesystemIO__new()
{
    return FileIO__new_all_unsupported();
}

void * Ext2FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors
) __attribute__((weak));
void * Ext2FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return 0;
}

BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries() __attribute__((weak));
BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries()
{
    return EmptyArray(BlockDeviceMetadataEntries);
}

BString embedded = bstr("sbin");
BString dev = bstr("dev");
BString fat16 = bstr("fat16");
BString fat32 = bstr("fat32");
BString ext2 = bstr("ext2");
BString memfs = bstr("tmp/memfs");
BString proc = bstr("proc");

#define BOOT_CONFIG_BLOCK_COUNT ((uint64_t) 8 * 1024 * 1024 / BLOCK_DEVICE_BLOCK_SIZE)

Optional(BlockDevicePtr) BlockDeviceRegistry__find(ConstBStringPtr path) __attribute__((weak));
Optional(BlockDevicePtr) BlockDeviceRegistry__find(ConstBStringPtr path)
{
    return None(BlockDevicePtr);
}

static Result(IOStatus) Filesystem__mount_resolved_block_device(
    FilesystemPtr root_fs, BlockDevicePtr device, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    Optional(FilesystemPtr) filesystem = None(FilesystemPtr);

    if (BString__equals(filesystem_type, &fat16) && Fat16FilesystemIO__available())
    {
        FileIO fat16_io = Fat16FilesystemIO__new();
        FilesystemPtr fat_filesystem = Filesystem__heap_new(mountpoint, &fat16_io);
        Fat16FilesystemIOState__heap_new(fat_filesystem, device, 0);
        filesystem = Some(FilesystemPtr, fat_filesystem);
    }
    else if (BString__equals(filesystem_type, &fat32) && Fat32FilesystemIO__available())
    {
        FileIO fat32_io = Fat32FilesystemIO__new();
        FilesystemPtr fat_filesystem = Filesystem__heap_new(mountpoint, &fat32_io);
        Fat32FilesystemIOState__heap_new(fat_filesystem, device, 0);
        filesystem = Some(FilesystemPtr, fat_filesystem);
    }
    else if (BString__equals(filesystem_type, &bstr("ext2")) && Ext2FilesystemIO__available())
    {
        FileIO ext2_io = Ext2FilesystemIO__new();
        FilesystemPtr ext2_filesystem = Filesystem__heap_new(mountpoint, &ext2_io);
        Ext2FilesystemIOState__heap_new(ext2_filesystem, device, 0);
        filesystem = Some(FilesystemPtr, ext2_filesystem);
    }

    match_optional(FilesystemPtr, filesystem,
        some(mounted_filesystem)
        {
            match_result(MountResult, Filesystem__mount(root_fs, mountpoint, mounted_filesystem),
                success(_) { return Success(IOStatus, IO_OK); },
                error(code, message) { return Error(IOStatus, message, code); }
            );
        },
        none_do_nothing()
    );

    return Error(IOStatus, bstring("Filesystem type not supported"), IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) Filesystem__mount_block_device(
    FilesystemPtr self, ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    match_optional(FilesystemPtr, Mounts__get(&self->mounts, *mountpoint),
        some(existing) { return Success(IOStatus, IO_OK); },
        none_do_nothing()
    );

    match_optional(BlockDevicePtr, BlockDeviceRegistry__find(device_path),
        some(device) { return Filesystem__mount_resolved_block_device(self, device, filesystem_type, mountpoint); },
        none_do_nothing()
    );

    return Error(IOStatus, bstring("Block device not found"), IO_INVALID_PATH);
}

static void FilesystemTable__mount_entry(FilesystemPtr root_fs, FilesystemTableEntryPtr entry)
{
    match_optional(FilesystemPtr, Mounts__get(&root_fs->mounts, entry->mountpoint),
        some(existing) { return; },
        none_do_nothing()
    );

    Optional(BlockDevicePtr) device_match = None(BlockDevicePtr);
    if (entry->match_block_count)
    {
        BlockDeviceMetadataEntries metadata_entries = BlockDeviceRegistry__metadata_entries();
        for_each_fa(BlockDeviceMetadataEntries, metadata_entries, index, metadata,
        {
            if (metadata.block_count == entry->block_count)
            {
                device_match = BlockDeviceRegistry__find(&metadata.path);
            }
        });
    }
    else
    {
        device_match = BlockDeviceRegistry__find(&entry->device_path);
    }

    match_optional(BlockDevicePtr, device_match,
        some(device) { Filesystem__mount_resolved_block_device(root_fs, device, &entry->filesystem_type, &entry->mountpoint); },
        none_do_nothing()
    );
}

static Result(CollectionCode) FilesystemTable__add_hardcoded_entries(FilesystemTablePtr self)
{
    match_result(CollectionCode, FilesystemTable__add(
        self,
        &bstr("/dev/virtio1"),
        &bstr("ext2"),
        &bstr("/")
    ),
        success_do_nothing(),
        error(code, message) { return Error(CollectionCode, message, code); }
    );

    match_result(CollectionCode, FilesystemTable__add_by_block_count(
        self,
        BOOT_CONFIG_BLOCK_COUNT,
        &bstr("ext2"),
        &bstr("/boot/config")
    ),
        success_do_nothing(),
        error(code, message) { return Error(CollectionCode, message, code); }
    );

    return Success(CollectionCode, 0);
}

void FilesystemTable__mount_available_entries(FilesystemPtr root_fs)
{
    for_each_ll(FilesystemTableEntries, root_fs->filesystem_table.entries, entry,
    {
        FilesystemTable__mount_entry(root_fs, &entry);
    });
}

FilesystemPtr Filesystem__build_root_and_mounts(KernelPtr kernel)
{
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_fs_state = FilesystemIOState__heap_new(root_fs);

    FileIO embedded_io = EmbeddedFilesystemIO__new();
    FilesystemPtr embedded_fs = Filesystem__heap_new(&embedded, &embedded_io);
    EmbeddedFilesystemIOStatePtr embedded_fs_state = EmbeddedFilesystemIOState__heap_new(embedded_fs);

    Filesystem__mount(root_fs, &embedded, embedded_fs);

    FileIO dev_io = DevFilesystemIO__new();
    FilesystemPtr dev_filesystem = Filesystem__heap_new(&dev, &dev_io);
    DevFilesystemIOStatePtr devfs_state = DevFilesystemIOState__heap_new(dev_filesystem, kernel->io_subsystem);
    Filesystem__mount(root_fs, &dev, dev_filesystem);

    FileIO proc_io = ProcFilesystemIO__new();
    FilesystemPtr proc_filesystem = Filesystem__heap_new(&proc, &proc_io);
    ProcFilesystemIOStatePtr procfs_state = ProcFilesystemIOState__heap_new(proc_filesystem, kernel);
    Filesystem__mount(root_fs, &proc, proc_filesystem);

    FileIO mem_io = MemFilesystemIO__new();
    FilesystemPtr mem_filesystem = Filesystem__heap_new(&memfs, &mem_io);
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(mem_filesystem);
    Filesystem__mount(root_fs, &memfs, mem_filesystem);

    match_result(CollectionCode, FilesystemTable__add_hardcoded_entries(&root_fs->filesystem_table),
        success(_) { FilesystemTable__mount_available_entries(root_fs); },
        error(code, message) { __d_printf(FILESYSTEM, "Filesystem table setup failed %i %s\n", code, cstr(message)); }
    );

    __d_printf(FILESYSTEM, "Root filesystem %p\n", root_fs);
    __d_printf(FILESYSTEM, "Embedded filesystem %p\n", embedded_fs);
    __d_printf(FILESYSTEM, "Device filesystem %p\n", dev_filesystem);
    __d_printf(FILESYSTEM, "Proc filesystem %p\n", proc_filesystem);
    __d_printf(FILESYSTEM, "Memory filesystem %p\n", mem_filesystem);
    return root_fs;
}

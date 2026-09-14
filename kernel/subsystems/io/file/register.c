#include "register.h"

#include "../../../kernel_core.h"
#include "../block/block_device.h"
#include "filesystem.h"
#include "bootfs/bootfs.h"

static FileIO FileIO__new_all_unsupported()
{
    return (FileIO){
        .base_io = IO__new(IO__unsupported_open, IO__unsupported_read, IO__unsupported_write, IO__unsupported_accept,
                           IO__unsupported_close, IO__unsupported_park, IO__unsupported_unpark,
                           IO__unsupported_server_buffer, IO__unsupported_server_write_buffer,
                           IO__unsupported_data_available, IO__unsupported_data_consumed, IO__unsupported_eof),
        .list_nodes = FileIO__unsupported_list_nodes,
        .list_dir = FileIO__unsupported_list_dir,
        .mkdir = FileIO__unsupported_mkdir,
        .remove = FileIO__unsupported_remove,
        .link = FileIO__unsupported_link,
    };
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

void * Ext2FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
    __attribute__((weak));
void * Ext2FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return 0;
}

BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries() __attribute__((weak));
BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries()
{
    return EmptyArray(BlockDeviceMetadataEntries);
}

BString bootstrap_ext2 = bstr("bootstrap-ext2");
BString boot = bstr("boot");

#define BOOT_CONFIG_BLOCK_COUNT ((uint64_t)8 * 1024 * 1024 / BLOCK_DEVICE_BLOCK_SIZE)

Optional(BlockDevicePtr) BlockDeviceRegistry__find(ConstBStringPtr path) __attribute__((weak));
Optional(BlockDevicePtr) BlockDeviceRegistry__find(ConstBStringPtr path)
{
    return None(BlockDevicePtr);
}

static Result(IOStatus)
    Filesystem__mount_resolved_block_device(FilesystemPtr root_fs, BlockDevicePtr device,
                                            ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    Optional(FilesystemPtr) filesystem = None(FilesystemPtr);

    if (BString__equals(filesystem_type, &bootstrap_ext2) && Ext2FilesystemIO__available())
    {
        FileIO ext2_io = Ext2FilesystemIO__new();
        FilesystemPtr ext2_filesystem = Filesystem__heap_new(mountpoint, &ext2_io);
        Ext2FilesystemIOState__heap_new(ext2_filesystem, device, 0);
        filesystem = Some(FilesystemPtr, ext2_filesystem);
    }

    match_optional(
        filesystem,
        some(mounted_filesystem)
        {
            match_result(
                Filesystem__mount(root_fs, mountpoint, mounted_filesystem),
                success(_) { return Success(IOStatus, IO_OK); },
                error(code, message) { return Error(IOStatus, message, code); }
            );
        },
        none_do_nothing()
    );

    return Error(IOStatus, bstring("Filesystem type not supported"), IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) Filesystem__mount_block_device(FilesystemPtr self, ConstBStringPtr device_path,
                                                ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    match_optional(
        Mounts__get(&self->mounts, *mountpoint),
        some(existing) { return Success(IOStatus, IO_OK); },
        none_do_nothing()
    );

    match_optional(
        BlockDeviceRegistry__find(device_path),
        some(device) { return Filesystem__mount_resolved_block_device(self, device, filesystem_type, mountpoint); },
        none_do_nothing()
    );

    return Error(IOStatus, bstring("Block device not found"), IO_INVALID_PATH);
}

static void FilesystemTable__mount_entry(FilesystemPtr root_fs, FilesystemTableEntryPtr entry)
{
    match_optional(Mounts__get(&root_fs->mounts, entry->mountpoint),
        some(existing) { return; },
        none_do_nothing()
    );

    Optional(BlockDevicePtr) device_match = None(BlockDevicePtr);
    if (entry->match_block_count)
    {
        BlockDeviceMetadataEntries metadata_entries = BlockDeviceRegistry__metadata_entries();
        for_each_fa(BlockDeviceMetadataEntries, metadata_entries, index, metadata, {
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

    match_optional(
        device_match,
        some(device)
        {
            Filesystem__mount_resolved_block_device(root_fs, device, &entry->filesystem_type, &entry->mountpoint);
        },
        none_do_nothing()
    );
}

static Result(CollectionCode) FilesystemTable__add_hardcoded_entries(FilesystemTablePtr self)
{
    match_result(
        FilesystemTable__add_by_block_count(self, BOOT_CONFIG_BLOCK_COUNT, &bootstrap_ext2, &bstr("/boot/config")),
        success_do_nothing(),
        error(code, message) { return Error(CollectionCode, message, code); }
    );

    return Success(CollectionCode, 0);
}

void FilesystemTable__mount_available_entries(FilesystemPtr root_fs)
{
    for_each_ll(FilesystemTableEntries, root_fs->filesystem_table.entries, entry,
                { FilesystemTable__mount_entry(root_fs, &entry); });
}

FilesystemPtr Filesystem__build_root_and_mounts(KernelPtr kernel)
{
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_fs_state = FilesystemIOState__heap_new(root_fs);

    FileIO boot_io = BootFilesystemIO__new();
    FilesystemPtr boot_filesystem = Filesystem__heap_new(&boot, &boot_io);
    BootFilesystemIOStatePtr bootfs_state = BootFilesystemIOState__heap_new(boot_filesystem, kernel);
    Filesystem__mount(root_fs, &boot, boot_filesystem);

    match_result(
        FilesystemTable__add_hardcoded_entries(&root_fs->filesystem_table),
        success(_) { FilesystemTable__mount_available_entries(root_fs); },
        error(code, message) { __d_printf(FILESYSTEM, "Filesystem table setup failed %i %s\n", code, cstr(message)); }
    );

    __d_printf(FILESYSTEM, "Root filesystem %p\n", root_fs);
    __d_printf(FILESYSTEM, "Boot filesystem %p\n", boot_filesystem);
    return root_fs;
}

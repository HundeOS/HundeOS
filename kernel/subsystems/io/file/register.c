#include "register.h"

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

FileIO Fat16FilesystemIO__new() __attribute__((weak));
FileIO Fat16FilesystemIO__new()
{
    return (FileIO) { 0 };
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
    return (FileIO) { 0 };
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
    return (FileIO) { 0 };
}

void * Ext2FilesystemIOState__heap_new(
    FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors
) __attribute__((weak));
void * Ext2FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return 0;
}

BString embedded = BSTRING_NEW("sbin");
BString dev = BSTRING_NEW("dev");
BString fat16 = BSTRING_NEW("fat16");
BString fat32 = BSTRING_NEW("fat32");
BString ext2 = BSTRING_NEW("ext2");
BString memfs = BSTRING_NEW("tmp/memfs");
BString proc = BSTRING_NEW("proc");

Optional(BlockDevicePtr) BlockDeviceRegistry__find(BString path) __attribute__((weak));
Optional(BlockDevicePtr) BlockDeviceRegistry__find(BString path)
{
    return None(BlockDevicePtr);
}

static void FilesystemTable__mount_entry(FilesystemPtr root_fs, FilesystemTableEntryPtr entry)
{
    Match(FilesystemPtr, Mounts__get(&root_fs->mounts, entry->mountpoint))
        IsSome(existing, return)
    End

    Optional(FilesystemPtr) filesystem = None(FilesystemPtr);

    Match(BlockDevicePtr, BlockDeviceRegistry__find(entry->device_path))
        IsSome(device,
        {
            if (BString__equals(&entry->filesystem_type, &fat16) && Fat16FilesystemIO__available())
            {
                FilesystemPtr fat_filesystem = Filesystem__heap_new(entry->mountpoint, Fat16FilesystemIO__new());
                void * state = Fat16FilesystemIOState__heap_new(fat_filesystem, device, 0);
                filesystem = Some(FilesystemPtr, fat_filesystem);
            }
            else if (BString__equals(&entry->filesystem_type, &fat32) && Fat32FilesystemIO__available())
            {
                FilesystemPtr fat_filesystem = Filesystem__heap_new(entry->mountpoint, Fat32FilesystemIO__new());
                void * state = Fat32FilesystemIOState__heap_new(fat_filesystem, device, 0);
                filesystem = Some(FilesystemPtr, fat_filesystem);
            }
            else if (BString__equals(&entry->filesystem_type, &ext2) && Ext2FilesystemIO__available())
            {
                FilesystemPtr ext2_filesystem = Filesystem__heap_new(entry->mountpoint, Ext2FilesystemIO__new());
                void * state = Ext2FilesystemIOState__heap_new(ext2_filesystem, device, 0);
                filesystem = Some(FilesystemPtr, ext2_filesystem);
            }
        })
    End

    Match(FilesystemPtr, filesystem)
        IsSome(mounted_filesystem, Filesystem__mount(root_fs, entry->mountpoint, mounted_filesystem))
    End
}

static Result(CollectionCode) FilesystemTable__add_hardcoded_entries(FilesystemTablePtr self)
{
    ResultMatch(CollectionCode, FilesystemTable__add(
        self,
        BSTRING_NEW("/dev/virtio1"),
        BSTRING_NEW("ext2"),
        BSTRING_NEW("/")
    ))
        IsSuccess(index, {})
        IsError(code, message, return Error(CollectionCode, message, code))
    End

    ResultMatch(CollectionCode, FilesystemTable__add(
        self,
        BSTRING_NEW("/dev/virtio2"),
        BSTRING_NEW("fat32"),
        BSTRING_NEW("/mnt/fs1")
    ))
        IsSuccess(index, {})
        IsError(code, message, return Error(CollectionCode, message, code))
    End

    return Success(CollectionCode, 0);
}

void FilesystemTable__mount_available_entries(FilesystemPtr root_fs)
{
    LLIterate(FilesystemTableEntries, root_fs->filesystem_table.entries, entry)
        FilesystemTable__mount_entry(root_fs, &entry);
    End
}

FilesystemPtr Filesystem__build_root_and_mounts(KernelPtr kernel)
{
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_fs_state = FilesystemIOState__heap_new(root_fs);

    FilesystemPtr embedded_fs = Filesystem__heap_new(embedded, EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_fs_state = EmbeddedFilesystemIOState__heap_new(embedded_fs);

    Filesystem__mount(root_fs, embedded, embedded_fs);

    FilesystemPtr dev_filesystem = Filesystem__heap_new(dev, DevFilesystemIO__new());
    DevFilesystemIOStatePtr devfs_state = DevFilesystemIOState__heap_new(dev_filesystem);
    Filesystem__mount(root_fs, dev, dev_filesystem);

    FilesystemPtr proc_filesystem = Filesystem__heap_new(proc, ProcFilesystemIO__new());
    ProcFilesystemIOStatePtr procfs_state = ProcFilesystemIOState__heap_new(proc_filesystem, kernel);
    Filesystem__mount(root_fs, proc, proc_filesystem);

    FilesystemPtr mem_filesystem = Filesystem__heap_new(memfs, MemFilesystemIO__new());
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(mem_filesystem);
    Filesystem__mount(root_fs, memfs, mem_filesystem);

    ResultMatch(CollectionCode, FilesystemTable__add_hardcoded_entries(&root_fs->filesystem_table))
        IsSuccess(_, FilesystemTable__mount_available_entries(root_fs))
        IsError(code, message, __d_printf(FILESYSTEM, "Filesystem table setup failed %i %s\n", code, cstr(message)))
    End

    __d_printf(FILESYSTEM, "Root filesystem %p\n", root_fs);
    __d_printf(FILESYSTEM, "Embedded filesystem %p\n", embedded_fs);
    __d_printf(FILESYSTEM, "Device filesystem %p\n", dev_filesystem);
    __d_printf(FILESYSTEM, "Proc filesystem %p\n", proc_filesystem);
    __d_printf(FILESYSTEM, "Memory filesystem %p\n", mem_filesystem);
    return root_fs;
}

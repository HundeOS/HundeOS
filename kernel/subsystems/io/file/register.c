#include "register.h"

#include "filesystem.h"
#include "embedded/embedded.h"
#include "memfs/memfs.h"

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

void * Fat16FilesystemIOState__heap_new(FilesystemPtr fs, uint64_t total_sectors) __attribute__((weak));
void * Fat16FilesystemIOState__heap_new(FilesystemPtr fs, uint64_t total_sectors)
{
    (void) fs;
    (void) total_sectors;
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

void * Fat32FilesystemIOState__heap_new(FilesystemPtr fs, uint64_t total_sectors) __attribute__((weak));
void * Fat32FilesystemIOState__heap_new(FilesystemPtr fs, uint64_t total_sectors)
{
    (void) fs;
    (void) total_sectors;
    return 0;
}

KString embedded = KSTRING_NEW("ebin");
KString fat16 = KSTRING_NEW("fat16");
KString fat32 = KSTRING_NEW("fat32");
KString memfs = KSTRING_NEW("tmp/memfs");

FilesystemPtr Filesystem__build_root_and_mounts()
{
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_fs_state = FilesystemIOState__heap_new(root_fs);
    (void) root_fs_state;

    FilesystemPtr embedded_fs = Filesystem__heap_new(embedded, EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_fs_state = EmbeddedFilesystemIOState__heap_new(embedded_fs);
    (void) embedded_fs_state;

    Filesystem__mount(root_fs, embedded, embedded_fs);

    FilesystemPtr mem_filesystem = Filesystem__heap_new(memfs, MemFilesystemIO__new());
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(mem_filesystem);
    (void) memfs_state;
    Filesystem__mount(root_fs, memfs, mem_filesystem);

    FilesystemPtr fat16_filesystem = 0;
    if (Fat16FilesystemIO__available())
    {
        fat16_filesystem = Filesystem__heap_new(fat16, Fat16FilesystemIO__new());
        void * fat16_state = Fat16FilesystemIOState__heap_new(fat16_filesystem, 0);
        (void) fat16_state;
        Filesystem__mount(root_fs, fat16, fat16_filesystem);
    }

    FilesystemPtr fat32_filesystem = 0;
    if (Fat32FilesystemIO__available())
    {
        fat32_filesystem = Filesystem__heap_new(fat32, Fat32FilesystemIO__new());
        void * fat32_state = Fat32FilesystemIOState__heap_new(fat32_filesystem, 0);
        (void) fat32_state;
        Filesystem__mount(root_fs, fat32, fat32_filesystem);
    }

    __d_printf(FILESYSTEM, "Root filesystem %p\n", root_fs);
    __d_printf(FILESYSTEM, "Embedded filesystem %p\n", embedded_fs);
    __d_printf(FILESYSTEM, "Memory filesystem %p\n", mem_filesystem);
    if (fat16_filesystem != 0)
    {
        __d_printf(FILESYSTEM, "FAT16 filesystem %p\n", fat16_filesystem);
    }
    if (fat32_filesystem != 0)
    {
        __d_printf(FILESYSTEM, "FAT32 filesystem %p\n", fat32_filesystem);
    }
    return root_fs;
}

#include "io_subsystem.h"
#include "embedded/embedded.h"
#include "../../../test/kernel/fixtures/memfs/memfs.h"
#include "../../../kernel/subsystems/io/file/bootfs/bootfs.h"
#include "kernel.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries(KernelPtr kernel)
{
    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FileIO embedded_io = EmbeddedFilesystemIO__new();
    FilesystemPtr embedded_binaries = Filesystem__heap_new(&bstr("embed"), &embedded_io);
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, &bstr("sbin"), embedded_binaries);

    FileIO boot_io = BootFilesystemIO__new();
    FilesystemPtr bootfs = Filesystem__heap_new(&bstr("boot"), &boot_io);
    BootFilesystemIOStatePtr bootfs_state = BootFilesystemIOState__heap_new(bootfs, kernel);

    Filesystem__mount(filesystem, &bstr("boot"), bootfs);

    FileIO memfs_io = MemFilesystemIO__new();
    FilesystemPtr memfs = Filesystem__heap_new(&bstr("tmp/memfs"), &memfs_io);
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(memfs);

    Filesystem__mount(filesystem, &bstr("tmp/memfs"), memfs);

    return filesystem;
}

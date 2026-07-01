#include "io_subsystem.h"
#include "../../../kernel/subsystems/io/file/devfs/devfs.h"
#include "../../../kernel/subsystems/io/file/embedded/embedded.h"
#include "../../../kernel/subsystems/io/file/memfs/memfs.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries()
{
    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FilesystemPtr embedded_binaries = Filesystem__heap_new(kstring("embed"), EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, kstring("ebin"), embedded_binaries);

    FilesystemPtr devfs = Filesystem__heap_new(kstring("dev"), DevFilesystemIO__new());
    DevFilesystemIOStatePtr devfs_state = DevFilesystemIOState__heap_new(devfs);

    Filesystem__mount(filesystem, kstring("dev"), devfs);

    FilesystemPtr memfs = Filesystem__heap_new(kstring("tmp/memfs"), MemFilesystemIO__new());
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(memfs);

    Filesystem__mount(filesystem, kstring("tmp/memfs"), memfs);

    return filesystem;
}

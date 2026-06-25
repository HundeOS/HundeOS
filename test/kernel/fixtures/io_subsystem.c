#include "io_subsystem.h"
#include "../../../kernel/subsystems/io/file/embedded/embedded.h"
#include "../../../kernel/subsystems/io/file/memfs/memfs.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries()
{
    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FilesystemPtr embedded_binaries = Filesystem__heap_new(kstring("embed"), EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, kstring("ebin"), embedded_binaries);

    FilesystemPtr memfs = Filesystem__heap_new(kstring("tmp/memfs"), MemFilesystemIO__new());
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(memfs);

    Filesystem__mount(filesystem, kstring("tmp/memfs"), memfs);

    (void) filesystem_state;
    (void) embedded_binaries_state;
    (void) memfs_state;

    return filesystem;
}

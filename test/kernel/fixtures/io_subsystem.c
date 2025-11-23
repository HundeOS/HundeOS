#include "io_subsystem.h"
#include "../../../kernel/subsystems/io/file/embedded/embedded.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries()
{
    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FilesystemPtr embedded_binaries = Filesystem__heap_new(kstring("embed"), EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, kstring("embedded"), embedded_binaries);

    return filesystem;
}
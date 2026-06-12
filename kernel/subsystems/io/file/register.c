#include "register.h"

#include "filesystem.h"
#include "embedded/embedded.h"

KString embedded = KSTRING_NEW("ebin");

FilesystemPtr Filesystem__build_root_and_mounts()
{
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_fs_state = FilesystemIOState__heap_new(root_fs);

    FilesystemPtr embedded_fs = Filesystem__heap_new(embedded, EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_fs_state = EmbeddedFilesystemIOState__heap_new(embedded_fs);

    Filesystem__mount(root_fs, embedded, embedded_fs);
    __d_printf(FILESYSTEM, "Root filesystem %p\n", root_fs);
    __d_printf(FILESYSTEM, "Embedded filesystem %p\n", embedded_fs);
    return root_fs;
}

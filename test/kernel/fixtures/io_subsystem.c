#include "io_subsystem.h"
#include "../../../kernel/subsystems/io/file/devfs/devfs.h"
#include "../../../kernel/subsystems/io/file/embedded/embedded.h"
#include "../../../kernel/subsystems/io/file/memfs/memfs.h"
#include "../../../kernel/subsystems/io/file/procfs/procfs.h"
#include "kernel.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries()
{
    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FilesystemPtr embedded_binaries = Filesystem__heap_new(bstring("embed"), EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, bstring("sbin"), embedded_binaries);

    FilesystemPtr devfs = Filesystem__heap_new(bstring("dev"), DevFilesystemIO__new());
    DevFilesystemIOStatePtr devfs_state = DevFilesystemIOState__heap_new(devfs);

    Filesystem__mount(filesystem, bstring("dev"), devfs);

    FilesystemPtr procfs = Filesystem__heap_new(bstring("proc"), ProcFilesystemIO__new());
    KernelPtr kernel = init_test_kernel_without_filesystem();
    ProcFilesystemIOStatePtr procfs_state = ProcFilesystemIOState__heap_new(procfs, kernel);

    Filesystem__mount(filesystem, bstring("proc"), procfs);

    FilesystemPtr memfs = Filesystem__heap_new(bstring("tmp/memfs"), MemFilesystemIO__new());
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(memfs);

    Filesystem__mount(filesystem, bstring("tmp/memfs"), memfs);

    return filesystem;
}

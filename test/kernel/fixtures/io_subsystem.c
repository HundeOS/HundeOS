#include "io_subsystem.h"
#include "../../../kernel/subsystems/io/file/devfs/devfs.h"
#include "../../../kernel/subsystems/io/file/embedded/embedded.h"
#include "../../../kernel/subsystems/io/file/memfs/memfs.h"
#include "../../../kernel/subsystems/io/file/procfs/procfs.h"
#include "kernel.h"

FilesystemPtr init_root_filesystem_with_embedded_binaries(KernelPtr kernel)
{
    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FileIO embedded_io = EmbeddedFilesystemIO__new();
    FilesystemPtr embedded_binaries = Filesystem__heap_new(&bstr("embed"), &embedded_io);
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, &bstr("sbin"), embedded_binaries);

    FileIO dev_io = DevFilesystemIO__new();
    FilesystemPtr devfs = Filesystem__heap_new(&bstr("dev"), &dev_io);
    DevFilesystemIOStatePtr devfs_state = DevFilesystemIOState__heap_new(devfs, kernel->io_subsystem);

    Filesystem__mount(filesystem, &bstr("dev"), devfs);

    FileIO proc_io = ProcFilesystemIO__new();
    FilesystemPtr procfs = Filesystem__heap_new(&bstr("proc"), &proc_io);
    ProcFilesystemIOStatePtr procfs_state = ProcFilesystemIOState__heap_new(procfs, kernel);

    Filesystem__mount(filesystem, &bstr("proc"), procfs);

    FileIO memfs_io = MemFilesystemIO__new();
    FilesystemPtr memfs = Filesystem__heap_new(&bstr("tmp/memfs"), &memfs_io);
    MemFilesystemIOStatePtr memfs_state = MemFilesystemIOState__heap_new(memfs);

    Filesystem__mount(filesystem, &bstr("tmp/memfs"), memfs);

    return filesystem;
}

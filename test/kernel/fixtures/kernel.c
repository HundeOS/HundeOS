#include "kernel.h"
#include "process.h"
#include "unity.h"
#include "../../../kernel/subsystems/io/file/filesystem.h"
#include "../../../kernel/subsystems/io/file/embedded/embedded.h"

KernelPtr init_test_kernel()
{
    SchedulerPtr scheduler = Scheduler__heap_new();
    KernelPtr kernel = Kernel__heap_new(scheduler);

    FilesystemPtr filesystem = RootFilesystem__heap_new();
    FilesystemIOStatePtr filesystem_state = FilesystemIOState__heap_new(filesystem);

    FilesystemPtr embedded_binaries = Filesystem__heap_new(kstring("embed"), EmbeddedFilesystemIO__new());
    EmbeddedFilesystemIOStatePtr embedded_binaries_state = EmbeddedFilesystemIOState__heap_new(embedded_binaries);

    Filesystem__mount(filesystem, kstring("embedded"), embedded_binaries);

    Result(DeviceHandle) register_res = IOSubsystem__register_device(
        &kernel->io_subsystem, kstring("/"), &filesystem->file_io.base_io, &filesystem->state->state
    );

    ResultMatch(DeviceHandle, register_res)
        IsError(code, message, TEST_FAIL())
    End

    Elf idle = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    Scheduler__init(kernel->scheduler, idle);
    Scheduler__start(kernel->scheduler, 0);

    return kernel;
}

#include "kernel.h"
#include "process.h"
#include "io_subsystem.h"

#include "unity.h"

KernelPtr init_test_kernel_without_filesystem()
{
    MessageBrokerPtr message_broker = MessageBroker__heap_new();
    SchedulerPtr scheduler = Scheduler__heap_new(message_broker);
    IOSubsystemPtr io_subsystem = IOSubsystem__heap_new(scheduler, message_broker);

    KernelPtr kernel = Kernel__heap_new(scheduler, message_broker, io_subsystem);

    Elf idle = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});

    Scheduler__init(scheduler, idle);
    Scheduler__start(scheduler, 0);

    return kernel;
}

KernelPtr init_test_kernel_with_filesystem(FilesystemPtr filesystem)
{
    KernelPtr kernel = init_test_kernel_without_filesystem();
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, filesystem);
    Result(DeviceHandle) register_res = IOSubsystem__register_device(
        kernel->io_subsystem, bstring("/"), &filesystem->file_io.base_io, &filesystem->state->state
    );
    ResultMatch(DeviceHandle, register_res)
        IsError(code, message, TEST_FAIL())
    End

    return kernel;
}


KernelPtr init_test_kernel()
{
    return init_test_kernel_with_filesystem(init_root_filesystem_with_embedded_binaries());
}

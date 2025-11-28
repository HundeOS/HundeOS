#include "kernel.h"
#include "process.h"
#include "io_subsystem.h"

#include "unity.h"

KernelPtr init_test_kernel_without_filesystem()
{
    MessageBrokerPtr message_broker = MessageBroker__heap_new();
    SchedulerPtr scheduler = Scheduler__heap_new(message_broker);
    KernelPtr kernel = Kernel__heap_new(scheduler, message_broker);

    Elf idle = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    Scheduler__init(kernel->scheduler, idle);
    Scheduler__start(kernel->scheduler, 0);

    return kernel;
}

KernelPtr init_test_kernel_with_filesystem(FilesystemPtr filesystem)
{
    KernelPtr kernel = init_test_kernel_without_filesystem();

    Result(DeviceHandle) register_res = IOSubsystem__register_device(
        &kernel->io_subsystem, kstring("/"), &filesystem->file_io.base_io, &filesystem->state->state
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

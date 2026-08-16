#include "kernel.h"
#include "process.h"
#include "io_subsystem.h"

#include "unity.h"
#include "../../../kernel/subsystems/message/message_broker.h"
#include "../../../kernel/irq/irq_dispatcher.h"
#include "../../../kernel/kalloc.h"

static KernelPtr TestKernel__heap_new(
    SchedulerPtr scheduler,
    MessageBrokerPtr message_broker,
    IOSubsystemPtr io_subsystem
)
{
    KernelPtr kernel = ptr(Kernel__alloc_kernel(sizeof(Kernel)), Kernel);

    kernel->status = INIT_NOT_STARTED;
    kernel->init_lock = MUTEX_INIT;
    kernel->syscall_dispatcher = NEW_SYSCALL_DISPATCHER;

    kernel->io_subsystem = io_subsystem;
    io_subsystem->scheduler = scheduler;

    kernel->system_info = Some(SystemInfo, ((SystemInfo) { 0 }));
    kernel->heap = NEW_HEAP;
    kernel->scheduler = scheduler;
    kernel->message_broker = message_broker;
    kernel->irq_dispatcher = ptr(Kernel__alloc_kernel(sizeof(IRQDispatcher)), IRQDispatcher);
    *kernel->irq_dispatcher = NEW_IRQ_DISPATCHER(scheduler);
    SystemInfoPtr system_info = &kernel->system_info.some.val;
    kernel->scheduler->interrupt_controller = Some(InterruptControllerInfo, system_info->interrupt_controller);
    kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, system_info->interrupt_controller);

    return kernel;
}

KernelPtr init_test_kernel_without_filesystem()
{
    MessageBrokerPtr message_broker = MessageBroker__heap_new();
    SchedulerPtr scheduler = Scheduler__heap_new(message_broker);
    IOSubsystemPtr io_subsystem = IOSubsystem__heap_new(scheduler, message_broker);

    KernelPtr kernel = TestKernel__heap_new(scheduler, message_broker, io_subsystem);
    ProcessFixture__set_kernel(kernel);

    Elf idle = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});

    Scheduler__init(kernel, scheduler, idle);
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

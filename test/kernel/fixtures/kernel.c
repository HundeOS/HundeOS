#include "kernel.h"
#include "process.h"
#include "io_subsystem.h"

#include "unity.h"
#include "../../../kernel/subsystems/message/message_broker.h"
#include "../../../kernel/irq/irq_dispatcher.h"
#include "../../../kernel/kalloc.h"
#include "../../../kernel/all_defs.h"

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

    SystemInfo system_info = SystemInfo__new();
    system_info.cpu_count = MAX_CPUS;
    kernel->system_info = Some(SystemInfo, system_info);
    kernel->heap = NEW_HEAP;
    kernel->heap.coarse_block_size = system_info.heap_coarse_block_size;
    kernel->scheduler = scheduler;
    kernel->message_broker = message_broker;
    kernel->irq_dispatcher = ptr(Kernel__alloc_kernel(sizeof(IRQDispatcher)), IRQDispatcher);
    *kernel->irq_dispatcher = NEW_IRQ_DISPATCHER(scheduler);
    kernel->auth = NEW_AUTH;
    match_optional(SystemInfo, kernel->system_info, some(value) {
            kernel->scheduler->interrupt_controller = Some(InterruptControllerInfo, value.interrupt_controller);
            kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, value.interrupt_controller);
        }, none_do_nothing());

    return kernel;
}

KernelPtr init_test_kernel_without_filesystem()
{
    MessageBrokerPtr message_broker = MessageBroker__heap_new();
    SchedulerPtr scheduler = Scheduler__heap_new(message_broker);
    IOSubsystemPtr io_subsystem = IOSubsystem__heap_new(scheduler, message_broker);

    KernelPtr kernel = TestKernel__heap_new(scheduler, message_broker, io_subsystem);
    ProcessFixture__set_kernel(kernel);

    Elf idle = unwrap(Elf, EmbeddedElf__load(&bstr("hello_world")), (Elf){});

    Scheduler__init(kernel, scheduler, idle);
    Scheduler__start(scheduler, 0);

    return kernel;
}

KernelPtr init_test_kernel_with_filesystem(FilesystemPtr filesystem)
{
    KernelPtr kernel = init_test_kernel_without_filesystem();
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, filesystem);
    Result(DeviceHandle) register_res = IOSubsystem__register_device(
        kernel->io_subsystem, &bstr("/"), &filesystem->file_io.base_io, &filesystem->state->state
    );
    match_result(DeviceHandle, register_res, success_do_nothing(), error_test_fail());

    return kernel;
}


KernelPtr init_test_kernel()
{
    KernelPtr kernel = init_test_kernel_without_filesystem();
    FilesystemPtr filesystem = init_root_filesystem_with_embedded_binaries(kernel);
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, filesystem);
    Result(DeviceHandle) register_res = IOSubsystem__register_device(
        kernel->io_subsystem, &bstr("/"), &filesystem->file_io.base_io, &filesystem->state->state
    );
    match_result(DeviceHandle, register_res, success_do_nothing(), error_test_fail());

    return kernel;
}

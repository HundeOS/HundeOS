#include "kernel.h"
#include "process.h"

#include "unity.h"
#include "../../../kernel/service/service_directory.h"
#include "../../../kernel/service/clock.h"
#include "../../../kernel/irq/irq_dispatcher.h"
#include "../../../kernel/kalloc.h"
#include "../../../kernel/all_defs.h"

static KernelPtr TestKernel__heap_new(SchedulerPtr scheduler)
{
    KernelPtr kernel = ptr(Kernel__alloc_kernel(sizeof(Kernel)), Kernel);

    kernel->status = INIT_NOT_STARTED;
    kernel->boot_authority = 0;
    kernel->boot_complete = false;
    kernel->init_lock = MUTEX_INIT;
    kernel->syscall_dispatcher = NEW_SYSCALL_DISPATCHER;

    SystemInfo system_info = SystemInfo__new();
    system_info.cpu_count = MAX_CPUS;
    system_info.realtime_clock = Some(RealtimeClockSample, RealtimeClockSample__new(1700000000000000ULL, 123000ULL));
    kernel->system_info = Some(SystemInfo, system_info);
    kernel->heap = NEW_HEAP;
    kernel->heap.coarse_block_size = system_info.heap_coarse_block_size;
    kernel->scheduler = scheduler;
    kernel->irq_dispatcher = ptr(Kernel__alloc_kernel(sizeof(IRQDispatcher)), IRQDispatcher);
    *kernel->irq_dispatcher = NEW_IRQ_DISPATCHER(scheduler);
    kernel->service_directory = KERNEL_SERVICE_DIRECTORY_INIT;
    kernel->device_registry = KernelDeviceRegistry__new();
    kernel->process_registry = KernelProcessRegistry__new();
    kernel->clock = KernelClock__new();
    kernel->memory_accounting = MemoryAccounting__new();
    match_result(
        KernelServiceDirectory__init(&kernel->service_directory),
        success_do_nothing(),
        error(code, message)
        {
            TEST_FAIL();
        }
    );
    match_result(
        KernelClock__init(&kernel->clock, system_info.timer_frequency, &system_info.realtime_clock),
        success_do_nothing(),
        error(code, message)
        {
            TEST_FAIL();
        }
    );
    kernel->auth = NEW_AUTH;
    match_optional(
        kernel->system_info,
        some(value)
        {
            kernel->scheduler->interrupt_controller = Some(InterruptControllerInfo, value.interrupt_controller);
            kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, value.interrupt_controller);
        },
        none_do_nothing()
    );

    return kernel;
}

KernelPtr init_test_kernel()
{
    SchedulerPtr scheduler = Scheduler__heap_new();

    KernelPtr kernel = TestKernel__heap_new(scheduler);
    ProcessFixture__set_kernel(kernel);

    Elf idle = unwrap_optional(EmbeddedElf__load(&bstr("hello_world")), (Elf){});

    Scheduler__init(kernel, scheduler, idle);
    for (CPUID cpu = 0; cpu < MAX_CPUS; cpu++)
    {
        Scheduler__register_cpu(scheduler, cpu);
    }
    Scheduler__start(scheduler, 0);

    return kernel;
}

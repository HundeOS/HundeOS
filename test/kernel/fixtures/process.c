#include "process.h"
#include "unity.h"
#include "../../../kernel/kernel_init.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/service/clock.h"

static KernelPtr test_kernel = NULL;

void ProcessFixture__set_kernel(KernelPtr kernel)
{
    test_kernel = kernel;
}

static KernelPtr ProcessFixture__kernel()
{
    KernelPtr kernel;
    if (test_kernel != NULL)
    {
        kernel = test_kernel;
    }
    else
    {
        kernel = Kernel__instance();
    }

    if (!kernel->clock.mapping.valid)
    {
        Optional(RealtimeClockSample) realtime_clock = None(RealtimeClockSample);
        match_result(KernelClock__init(&kernel->clock, 0, &realtime_clock),
            success_do_nothing(),
            error_test_fail()
        );
    }
    return kernel;
}

ProcessPtr dummy_process(Tick last_tick, CPUID cpu, ProcessState state)
{
    Elf hello_world = unwrap_optional(EmbeddedElf__load(&bstr("hello_world")), (Elf){});
    match_result(
        Process__setup(ProcessFixture__kernel(), &hello_world, &None(ProcessPtr)),
        success(process)
        {
            process->last_tick = last_tick;
            process->pid = last_tick * MAX_CPUS + cpu;
            process->state = state;

            return process;
        },
        error_test_fail()
    );
    return NULL;
}

ProcessPtr dummy_process_with_parent(Tick last_tick, CPUID cpu, ProcessState state, ProcessPtr parent)
{
    Elf hello_world = unwrap_optional(EmbeddedElf__load(&bstr("hello_world")), (Elf){});
    match_result(
        Process__setup(ProcessFixture__kernel(), &hello_world, &Some(ProcessPtr, parent)),
        success(process)
        {
            process->last_tick = last_tick;
            process->pid = last_tick * MAX_CPUS + cpu;
            process->state = state;

            return process;
        },
        error_test_fail()
    );
    return NULL;
}

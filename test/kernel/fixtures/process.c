#include "process.h"
#include "unity.h"
#include "../../../kernel/kernel_init.h"

static KernelPtr test_kernel = NULL;

void ProcessFixture__set_kernel(KernelPtr kernel)
{
    test_kernel = kernel;
}

static KernelPtr ProcessFixture__kernel()
{
    if (test_kernel != NULL)
    {
        return test_kernel;
    }

    return Kernel__instance();
}

ProcessPtr dummy_process(Tick last_tick, CPUID cpu, ProcessState state)
{
    Elf hello_world = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(Kernel__instance(), hello_world, None(ProcessPtr)))
        IsSuccess(process,
        {
            process->kernel = ProcessFixture__kernel();
            process->last_tick = last_tick;
            process->pid = last_tick * MAX_CPUS + cpu;
            process->state = state;

            return process;
        });
        IsError(code, message, TEST_FAIL())
    End
    return NULL;
}

ProcessPtr dummy_process_with_parent(Tick last_tick, CPUID cpu, ProcessState state, ProcessPtr parent)
{
    Elf hello_world = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(Kernel__instance(), hello_world, Some(ProcessPtr, parent)))
        IsSuccess(process,
        {
            process->kernel = ProcessFixture__kernel();
            process->last_tick = last_tick;
            process->pid = last_tick * MAX_CPUS + cpu;
            process->state = state;

            return process;
        });
        IsError(code, message, TEST_FAIL())
    End
    return NULL;
}

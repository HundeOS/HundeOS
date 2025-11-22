#include "process.h"

ProcessPtr dummy_process(Tick last_tick, CPUID cpu, ProcessState state)
{
    Elf hello_world = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(hello_world, None(ProcessPtr)))
        IsSuccess(process,
        {
            process->last_tick = last_tick;
            process->pid = last_tick * MAX_CPUS + cpu;
            process->state = state;

            return process;
        });
        IsError(code, message, TEST_FAIL())
    End
    return NULL;
}

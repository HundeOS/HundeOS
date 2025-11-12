#include <stdint.h>

#include "debug.h"
#include "kernel_core.h"
#include "kernel_init.h"
#include "panic.h"
#include "process.h"
#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "arch/panic.h"
#include "bin/embedded_binaries.h"
#include "types/kstring.h"
#include "types/result.h"
#include "../arch/aarch64/io/uart.h"


void schedule(SchedulerPtr scheduler, const char * name)
{
    __d_printf(PROGRESS, "Queuing to scheduler: %s\n", name);
    ElfPtr program = EmbeddedElf__load(name);
    ResultMatch(ProcessPtr, Process__setup(program))
        IsSuccess(process, Scheduler__schedule(scheduler, process))
        IsError(code, message,
        {
            __d_printf(PANIC, "Program failed: %s\n", name);
            Kernel__panic(cstr(message), NULL);
        })
    End
}


void Kernel_main(CPUID cpu_id) {
    KernelPtr kernel = Kernel__instance();
    __d_printf(PROGRESS, "Kernel starting init: %i\n", cpu_id);
    if (cpu_id == 1)
    {
        __d_printf(PROGRESS, "Kernel starting init: %i\n", cpu_id);
        Kernel__init(kernel);
    }
    else
    {
        Kernel__wait_for_init(kernel);
    }

    if (kernel->status != INIT_DONE)
    {
        __d_printf(PANIC, "Kernel init failed: %i\n", cpu_id);
        Kernel__panic("Kernel not initialised", NULL);
        return;
    }

    ResultMatch(InitCode, CPU__init());
        IsError(code, message,
        {
            __d_printf(PANIC, "CPU init failed: %i\n", cpu_id);
            Kernel__panic(KString__cstr(&message), NULL);
            return;
        });
    End

    __d_printf(PROGRESS, "Kernel starting loading program: %i\n", cpu_id);

    schedule(&kernel->scheduler, "shell");

    __d_printf(PROGRESS, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(1000);
    __d_printf(PROGRESS, "Timer started: %i\n", cpu_id);

    __d_printf(PROGRESS, "Kernel starting scheduler: %i\n", cpu_id);

    // starts the idle process and wont return
    Scheduler__start(&kernel->scheduler, cpu_id - 1);

    CPU__wait();
    Kernel__halt();
}
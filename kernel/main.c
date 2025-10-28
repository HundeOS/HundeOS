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


void Kernel_main(CPUID cpu_id) {
    KernelPtr kernel = Kernel__instance();
    __d_printf(FOCUS, "Kernel starting init: %i\n", cpu_id);
    if (cpu_id == 1)
    {
        __d_printf(FOCUS, "Kernel starting init: %i\n", cpu_id);
        Kernel__init(kernel);
    }
    else
    {
        Kernel__wait_for_init(kernel);
    }

    if (kernel->status != INIT_DONE)
    {
        __d_printf(FOCUS, "Kernel init failed: %i\n", cpu_id);
        Kernel__panic("Kernel not initialised", NULL);
        return;
    }

    ResultMatch(InitCode, CPU__init());
        IsError(code, message,
        {
            __d_printf(FOCUS, "CPU init failed: %i\n", cpu_id);
            Kernel__panic(KString__cstr(&message), NULL);
            return;
        });
    End

    __d_printf(DEBUG, "Kernel starting loading program: %i\n", cpu_id);
    Elf * program = EmbeddedElf__load("shell");

    __d_printf(PROGRESS, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(10000);
    __d_printf(PROGRESS, "Timer started: %i\n", cpu_id);

    __d_printf(DEBUG, "Kernel starting program: %i\n", cpu_id);
    Process__spawn_from_elf(program);
    __d_println(FOCUS, "Here?");
    CPU__wait();
    Kernel__halt();
}
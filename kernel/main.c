#include <stdint.h>

#include "spin.h"
#include "debug.h"
#include "kernel_init.h"
#include "panic.h"
#include "process.h"
#include "../arch/aarch64/boot/boot_debug.h"
#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "arch/panic.h"
#include "bin/embedded_binaries.h"

extern void dummy();
// static inline uint64_t rdsp(void){ uint64_t sp; __asm__ volatile("mov %0, sp":"=r"(sp)); return sp; }
void Kernel_main(CPUID cpu_id) {
    KernelPtr kernel = Kernel__instance();

    if (cpu_id == 1)
    {
        Kernel__init(kernel);
    }
    else
    {
        Kernel__wait_for_init(kernel);
    }

    if (kernel->status != INIT_DONE)
    {
        Kernel__panic("Kernel not initialised", NULL);
        return;
    }
    __d_printf(PROGRESS, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(10000);
    __d_printf(PROGRESS, "Timer started: %i\n", cpu_id);

    __d_printf(DEBUG, "Kernel starting loading program: %i\n", cpu_id);
    Elf * program = EmbeddedElf__load("hello_world");
    __d_printf(DEBUG, "Kernel starting program: %i\n", cpu_id);
    Process__spawn_from_elf(program);
    __d_printf(DEBUG, "Timer started: %i\n", cpu_id);
    CPU__wait_for_interrupts();
    Kernel__halt();
}
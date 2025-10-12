#include <stdint.h>

#include "spin.h"
#include "debug.h"
#include "process.h"
#include "../arch/aarch64/boot/boot_debug.h"
#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "arch/panic.h"
#include "bin/embedded_binaries.h"

extern void dummy();
// static inline uint64_t rdsp(void){ uint64_t sp; __asm__ volatile("mov %0, sp":"=r"(sp)); return sp; }
void Kernel_main(uint64_t cpu_id) {
    uint64_t * pointer = (uint64_t *)rdsp();

    __d_printf(FOCUS, "CPU entering main: %i\n", cpu_id);
    __d_printf(FOCUS, "SP: %p\n", pointer);
    *pointer = 0x12345678;
    __d_printf(FOCUS, "SP: %p\n", *(pointer--));
    __d_printf(FOCUS, "SP: %p\n", *(pointer--));
    CPUTimer__start(10000);
    __d_printf(DEBUG, "Timer started: %i\n", cpu_id);

    __d_printf(FOCUS, "Kernel starting loading program: %i\n", cpu_id);
    Elf * program = EmbeddedElf__load("hello_world");
    __d_printf(FOCUS, "Kernel starting program: %i\n", cpu_id);
    Process__spawn_from_elf(program);
    __d_printf(DEBUG, "Timer started: %i\n", cpu_id);
    CPU__wait_for_interrupts();
    Kernel__halt();
}
#include <stdint.h>

#include "debug.h"
#include "kernel_core.h"
#include "kernel_init.h"
#include "panic.h"
#include "process.h"
#include "exec/exec.h"
#include "arch/cpu.h"
#include "arch/cpu_timer.h"
#include "arch/panic.h"
#include "bin/embedded_binaries.h"
#include "types/bstring.h"
#include "types/result.h"
#include "syscalls/io.h"
#include "system_info.h"

BString panic_message = bstr("Program failed: %B\n");
static void install_default_stdio(KernelPtr kernel, ProcessPtr process)
{
    IOHandle input = IO_ENDPOINT_TTY_INPUT;
    match_optional(
        kernel->system_info,
        some(info)
        {
            match_optional(
                info.framebuffer,
                some(framebuffer)
                {
                    input = IO_ENDPOINT_KEYBOARD_INPUT;
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    process->startup_stdio = SpawnStdio__new(input, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    Process__prepare_startup_stdio(process);
}

void schedule_as(KernelPtr kernel, ConstBStringPtr name, ProcessClass process_class)
{
    __d_printf(PROGRESS, "Queuing to scheduler: %s\n", cstr_p(name));
    Optional(Elf) program = EmbeddedElf__load(name);
    match_optional(
        program,
        some(elf)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(process)
                {
                    __d_printf(PROCESS, "schedule_as setup success pid=%i name=%s process=%p\n", process->pid,
                               cstr(process->name), process);
                    process->process_class = process_class;
                    if (BString__equals(name, &bstr("services/hunde-mama")))
                    {
                        kernel->boot_authority = process->pid;
                    }
                    Exec__apply_builtin_identity_policy(process);
                    install_default_stdio(kernel, process);
                    __d_printf(SCHEDULER, "schedule_as scheduling pid=%i name=%s\n", process->pid, cstr(process->name));
                    Scheduler__schedule(kernel->scheduler, process);
                },
                error(code, message)
                {
                    __d_printf(PANIC, "Program failed: %s\n", cstr_p(name));
                    BString panic_error = BString__sprintf(&panic_message, name);
                    Kernel__panic(cstr(panic_error), NULL); // hundelint: ignore-no-null
                }
            );
        },
        none()
        {
            __d_printf(PANIC, "Program failed: %s\n", cstr_p(name));
            BString panic_error = BString__sprintf(&panic_message, name);
            Kernel__panic(cstr(panic_error), NULL); // hundelint: ignore-no-null
        }
    );
}

void schedule(KernelPtr kernel, ConstBStringPtr name)
{
    schedule_as(kernel, name, PROCESS_CLASS_NORMAL);
}

void Kernel_main(CPUID cpu_id)
{
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

    if (Kernel__init_status(kernel) != INIT_DONE)
    {
        __d_printf(PANIC, "Kernel init failed: %i\n", cpu_id);
        Kernel__panic("Kernel not initialised", NULL); // hundelint: ignore-no-null
        return;
    }

    SystemInfo system_info = SystemInfo__new();
    match_optional(
        kernel->system_info,
        some(value)
        {
            system_info = value;
            ;
        },
        none()
        {
            Kernel__panic("Kernel missing system info", NULL); // hundelint: ignore-no-null
            return;
        }
    );

    match_result(
        CPU__init(&system_info),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(PANIC, "CPU init failed: %i\n", cpu_id);
            Kernel__panic(BString__cstr(&message), NULL); // hundelint: ignore-no-null
            return;
        }
    );

    if (cpu_id == 1)
    {
        __d_printf(PROGRESS, "Kernel starting loading program: %i\n", cpu_id);
        schedule(kernel, &bstr("services/hunde-mama"));
        Scheduler__mark_ready(kernel->scheduler);
    }
    else
    {
        match_result(
            Scheduler__wait_until_ready(kernel->scheduler),
            success_do_nothing(),
            error(code, message)
            {
                Kernel__panic(BString__cstr(&message), NULL); // hundelint: ignore-no-null
                return;
            }
        );
    }

    __d_printf(PROGRESS, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(&system_info, 1000);
    __d_printf(PROGRESS, "Timer started: %i\n", cpu_id);

    __d_printf(PROGRESS, "Kernel starting scheduler: %i\n", cpu_id);

    // starts the idle process and wont return
    Scheduler__start(kernel->scheduler, cpu_id - 1);

    CPU__wait();
    Kernel__halt();
}

void Kernel__boot(void)
{
    Kernel_main(CPU__id());
}

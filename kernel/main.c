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
#include "subsystems/io/io_subsystem.h"
#include "syscalls/io.h"
#include "../arch/aarch64/io/uart.h"
#include "system_info.h"

BString panic_message = BSTRING_NEW("Program failed: %s\n");
static BString console_path = BSTRING_NEW(":console:");
static BString tty_path = BSTRING_NEW(":tty:");

static Result(IOHandle) open_stdio_handle(KernelPtr kernel, ProcessPtr process, BString path, Flags flags)
{
    __d_printf(
        IOSUB, "open_stdio_handle pid=%i name=%s path=%s flags=%i\n",
        process->pid, cstr(process->name), cstr(path), flags
    );
    KernelContext kernel_context = KernelContext__new(
        Some(ProcessID, process->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr context = IOContext__heap_new(
        kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), None(IOHandle), None(ExternalBuffer)
    );
    Result(IOHandle) open_result = IOSubsystem__open(kernel->io_subsystem, context, path, flags);
    IOContext__dispose(context);
    match_result(IOHandle, open_result,
        success(handle)
        {
            __d_printf(IOSUB, "open_stdio_handle success pid=%i flags=%i handle=%p\n", process->pid, flags, handle);
        },
        error(code, message)
        {
            __d_printf(IOSUB, "open_stdio_handle error pid=%i flags=%i code=%i message=%s\n", process->pid, flags, code, cstr(message));
        }
    );
    return open_result;
}

static Result(ProcessSetupStatus) install_default_stdio(KernelPtr kernel, ProcessPtr process)
{
    __d_printf(IOSUB, "install_default_stdio start pid=%i name=%s\n", process->pid, cstr(process->name));
    match_result(IOHandle, open_stdio_handle(kernel, process, tty_path, IO_OPEN_READ),
        success(handle)
        {
            __d_printf(IOSUB, "install_default_stdio fd=0 pid=%i handle=%p\n", process->pid, handle);
            Process__set_file_descriptor(process, 0, handle);
        },
        error(code, message) { return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP); }
    );

    match_result(IOHandle, open_stdio_handle(kernel, process, console_path, IO_OPEN_WRITE),
        success(handle)
        {
            __d_printf(IOSUB, "install_default_stdio fd=1 pid=%i handle=%p\n", process->pid, handle);
            Process__set_file_descriptor(process, 1, handle);
        },
        error(code, message) { return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP); }
    );

    match_result(IOHandle, open_stdio_handle(kernel, process, console_path, IO_OPEN_WRITE),
        success(handle)
        {
            __d_printf(IOSUB, "install_default_stdio fd=2 pid=%i handle=%p\n", process->pid, handle);
            Process__set_file_descriptor(process, 2, handle);
        },
        error(code, message) { return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP); }
    );

    __d_printf(IOSUB, "install_default_stdio done pid=%i name=%s\n", process->pid, cstr(process->name));
    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

void schedule_as(KernelPtr kernel, BString name, ProcessClass process_class)
{
    __d_printf(PROGRESS, "Queuing to scheduler: %s\n", cstr(name));
    Optional(Elf) program = EmbeddedElf__load(name);
    match_optional(Elf, program,
        some(elf)
        {
            match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)),
                success(process)
                {
                    __d_printf(PROCESS, "schedule_as setup success pid=%i name=%s process=%p\n", process->pid, cstr(process->name), process);
                    process->process_class = process_class;
                    Exec__apply_builtin_identity_policy(process);
                    match_result(ProcessSetupStatus, install_default_stdio(kernel, process),
                        success(_)
                        {
                            __d_printf(SCHEDULER, "schedule_as scheduling pid=%i name=%s\n", process->pid, cstr(process->name));
                            Scheduler__schedule(kernel->scheduler, process);
                        },
                        error(code, message)
                        {
                            Kernel__panic(BString__cstr(&message), NULL); // hundelint: ignore-no-null
                        }
                    );
                },
                error(code, message)
                {
                    __d_printf(PANIC, "Program failed: %s\n", cstr(name));
                    BString panic_error = BString__sprintf(&panic_message, cstr(name));
                    Kernel__panic(cstr(panic_error), NULL); // hundelint: ignore-no-null
                }
            );
        },
        none()
        {
            __d_printf(PANIC, "Program failed: %s\n", cstr(name));
            BString panic_error = BString__sprintf(&panic_message, cstr(name));
            Kernel__panic(cstr(panic_error), NULL); // hundelint: ignore-no-null
        }
    );
}

void schedule(KernelPtr kernel, BString name)
{
    schedule_as(kernel, name, PROCESS_CLASS_NORMAL);
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

    if (Kernel__init_status(kernel) != INIT_DONE)
    {
        __d_printf(PANIC, "Kernel init failed: %i\n", cpu_id);
        Kernel__panic("Kernel not initialised", NULL); // hundelint: ignore-no-null
        return;
    }

    SystemInfo system_info = (SystemInfo) {};
    match_optional(SystemInfo, kernel->system_info,
        some(value) { system_info = value;; },
        none()
        {
            Kernel__panic("Kernel missing system info", NULL); // hundelint: ignore-no-null
            return;
        }
    );

    match_result(InitCode, CPU__init(&system_info),
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
        schedule(kernel, bstring("hundeschule"));
        Scheduler__mark_ready(kernel->scheduler);
    }
    else
    {
        match_result(SchedulerInitStatus, Scheduler__wait_until_ready(kernel->scheduler),
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

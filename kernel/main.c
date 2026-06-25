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
#include "subsystems/io/io_subsystem.h"
#include "syscalls/io.h"
#include "../arch/aarch64/io/uart.h"

KString panic_message = KSTRING_NEW("Program failed: %s\n");
static KString console_path = KSTRING_NEW(":console:");

static Result(IOHandle) open_stdio_handle(KernelPtr kernel, ProcessPtr process, Flags flags)
{
    __d_printf(IOSUB, "open_stdio_handle pid=%i name=%s flags=%i\n", process->pid, cstr(process->name), flags);
    KernelContext kernel_context = KernelContext__new(
        Some(ProcessID, process->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr context = IOContext__heap_new(
        kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), None(IOHandle), None(ExternalBuffer)
    );
    Result(IOHandle) open_result = IOSubsystem__open(kernel->io_subsystem, context, console_path, flags);
    IOContext__dispose(context);
    ResultMatch(IOHandle, open_result)
        IsSuccess(handle, __d_printf(IOSUB, "open_stdio_handle success pid=%i flags=%i handle=%p\n", process->pid, flags, handle););
        IsError(code, message, __d_printf(IOSUB, "open_stdio_handle error pid=%i flags=%i code=%i message=%s\n", process->pid, flags, code, cstr(message)););
    End
    return open_result;
}

static Result(ProcessSetupStatus) install_default_stdio(KernelPtr kernel, ProcessPtr process)
{
    __d_printf(IOSUB, "install_default_stdio start pid=%i name=%s\n", process->pid, cstr(process->name));
    ResultMatch(IOHandle, open_stdio_handle(kernel, process, IO_OPEN_READ))
        IsSuccess(handle,
        {
            __d_printf(IOSUB, "install_default_stdio fd=0 pid=%i handle=%p\n", process->pid, handle);
            Process__set_file_descriptor(process, 0, handle);
        });
        IsError(code, message, return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP));
    End

    ResultMatch(IOHandle, open_stdio_handle(kernel, process, IO_OPEN_WRITE))
        IsSuccess(handle,
        {
            __d_printf(IOSUB, "install_default_stdio fd=1 pid=%i handle=%p\n", process->pid, handle);
            Process__set_file_descriptor(process, 1, handle);
        });
        IsError(code, message, return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP));
    End

    ResultMatch(IOHandle, open_stdio_handle(kernel, process, IO_OPEN_WRITE))
        IsSuccess(handle,
        {
            __d_printf(IOSUB, "install_default_stdio fd=2 pid=%i handle=%p\n", process->pid, handle);
            Process__set_file_descriptor(process, 2, handle);
        });
        IsError(code, message, return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP));
    End

    __d_printf(IOSUB, "install_default_stdio done pid=%i name=%s\n", process->pid, cstr(process->name));
    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

void schedule_as(KernelPtr kernel, KString name, ProcessClass process_class)
{
    __d_printf(PROGRESS, "Queuing to scheduler: %s\n", cstr(name));
    Optional(Elf) program = EmbeddedElf__load(name);
    Match(Elf, program)
        IsSome(elf,
        {
            ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
                IsSuccess(process,
                {
                    __d_printf(PROCESS, "schedule_as setup success pid=%i name=%s process=%p\n", process->pid, cstr(process->name), process);
                    process->process_class = process_class;
                    ResultMatch(ProcessSetupStatus, install_default_stdio(kernel, process))
                        IsSuccess(_,
                        {
                            __d_printf(SCHEDULER, "schedule_as scheduling pid=%i name=%s\n", process->pid, cstr(process->name));
                            Scheduler__schedule(kernel->scheduler, process);
                        });
                        IsError(code, message,
                        {
                            Kernel__panic(KString__cstr(&message), NULL);
                        })
                    End
                })
                IsError(code, message,
                {
                    __d_printf(PANIC, "Program failed: %s\n", cstr(name));
                    KString panic_error = KString__sprintf(&panic_message, cstr(name));
                    Kernel__panic(cstr(panic_error), NULL);
                })
            End
        });
        IsNone(
        {
            __d_printf(PANIC, "Program failed: %s\n", cstr(name));
            KString panic_error = KString__sprintf(&panic_message, cstr(name));
            Kernel__panic(cstr(panic_error), NULL);

        })
    End
}

void schedule(KernelPtr kernel, KString name)
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

    if (cpu_id == 1)
    {
        __d_printf(PROGRESS, "Kernel starting loading program: %i\n", cpu_id);
        schedule(kernel, kstring("shell"));
        Scheduler__mark_ready(kernel->scheduler);
    }
    else
    {
        ResultMatch(SchedulerInitStatus, Scheduler__wait_until_ready(kernel->scheduler))
            IsError(code, message,
            {
                Kernel__panic(KString__cstr(&message), NULL);
                return;
            });
        End
    }

    __d_printf(PROGRESS, "CPU entering main: %i\n", cpu_id);
    CPUTimer__start(1000);
    __d_printf(PROGRESS, "Timer started: %i\n", cpu_id);

    __d_printf(PROGRESS, "Kernel starting scheduler: %i\n", cpu_id);

    // starts the idle process and wont return
    Scheduler__start(kernel->scheduler, cpu_id - 1);

    CPU__wait();
    Kernel__halt();
}

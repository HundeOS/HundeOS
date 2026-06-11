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
    KernelContext kernel_context = KernelContext__new(
        Some(ProcessID, process->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr context = IOContext__heap_new(
        kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), None(IOHandle), None(ExternalBuffer)
    );
    Result(IOHandle) open_result = IOSubsystem__open(kernel->io_subsystem, context, console_path, flags);
    IOContext__dispose(context);
    return open_result;
}

static Result(ProcessSetupStatus) install_default_stdio(KernelPtr kernel, ProcessPtr process)
{
    ResultMatch(IOHandle, open_stdio_handle(kernel, process, IO_OPEN_READ))
        IsSuccess(handle, Process__set_file_descriptor(process, 0, handle));
        IsError(code, message, return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP));
    End

    ResultMatch(IOHandle, open_stdio_handle(kernel, process, IO_OPEN_WRITE))
        IsSuccess(handle, Process__set_file_descriptor(process, 1, handle));
        IsError(code, message, return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP));
    End

    ResultMatch(IOHandle, open_stdio_handle(kernel, process, IO_OPEN_WRITE))
        IsSuccess(handle, Process__set_file_descriptor(process, 2, handle));
        IsError(code, message, return Error(ProcessSetupStatus, message, PROCESS_FAILED_TO_SETUP));
    End

    return Success(ProcessSetupStatus, PROCESS_SETUP);
}

void schedule(KernelPtr kernel, KString name)
{
    __d_printf(PROGRESS, "Queuing to scheduler: %s\n", name);
    Optional(Elf) program = EmbeddedElf__load(name);
    Match(Elf, program)
        IsSome(elf,
        {
            ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
                IsSuccess(process,
                {
                    ResultMatch(ProcessSetupStatus, install_default_stdio(kernel, process))
                        IsSuccess(_, Scheduler__schedule(kernel->scheduler, process));
                        IsError(code, message,
                        {
                            Kernel__panic(KString__cstr(&message), NULL);
                        })
                    End
                })
                IsError(code, message,
                {
                    __d_printf(PANIC, "Program failed: %s\n", name);
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
